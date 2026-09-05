#include "fuse_vault/app.h"
#include "fuse_vault/input.h"
#include "fuse_vault/ui.h"
#include "host_services.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    DISPLAY_SCALE = 4,
};

typedef struct {
    fv_app_t app;
    fv_command_set_t last_commands;
    fv_platform_services_t services;
    fv_host_services_context_t services_context;
    bool persistence_enabled;
    fv_input_controller_t input_controller;
    uint32_t pressed_inputs;
    GtkWidget *display;
    GtkWidget *status;
} simulator_t;

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static fv_command_set_t execute_commands(simulator_t *simulator,
                                         fv_command_set_t commands) {
    fv_command_set_t observed = commands;
    if (!simulator->persistence_enabled) return observed;

    if ((commands & FV_COMMAND_STORE_ATTEMPT_COUNTER) != 0u) {
        fv_security_state_t state;
        if (simulator->services.ops->load_security_state(
                &simulator->services, &state) != FV_PERSIST_OK) {
            return observed | fv_app_handle(&simulator->app,
                                             FV_EVENT_FATAL_ERROR);
        }
        ++state.sequence;
        state.failed_attempts = simulator->app.failed_attempts;
        if (simulator->services.ops->store_security_state(
                &simulator->services, &state) != FV_PERSIST_OK) {
            secure_clear(&state, sizeof(state));
            return observed | fv_app_handle(&simulator->app,
                                             FV_EVENT_FATAL_ERROR);
        }
        secure_clear(&state, sizeof(state));
        observed |= fv_app_handle(&simulator->app,
                                  FV_EVENT_ATTEMPT_COUNTER_STORED);
    }

    if ((commands & FV_COMMAND_DESTROY_DEVICE_SECRET) != 0u) {
        (void)simulator->services.ops->revoke_device_secret(
            &simulator->services);
        fv_security_state_t state;
        if (simulator->services.ops->load_security_state(
                &simulator->services, &state) == FV_PERSIST_OK) {
            ++state.sequence;
            state.provisioned = false;
            (void)simulator->services.ops->store_security_state(
                &simulator->services, &state);
            secure_clear(&state, sizeof(state));
        }
    }
    return observed;
}

static gboolean draw_display(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    simulator_t *simulator = data;
    fv_framebuffer_t framebuffer;
    fv_ui_draw(&simulator->app, &framebuffer);

    cairo_scale(cr, DISPLAY_SCALE, DISPLAY_SCALE);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    for (unsigned y = 0u; y < FV_DISPLAY_HEIGHT; ++y) {
        unsigned x = 0u;
        while (x < FV_DISPLAY_WIDTH) {
            const fv_pixel_t pixel = framebuffer.pixels[y][x];
            unsigned end = x + 1u;
            while (end < FV_DISPLAY_WIDTH &&
                   framebuffer.pixels[y][end] == pixel) {
                ++end;
            }
            const double red = (double)((pixel >> 11u) & 0x1fu) / 31.0;
            const double green = (double)((pixel >> 5u) & 0x3fu) / 63.0;
            const double blue = (double)(pixel & 0x1fu) / 31.0;
            cairo_set_source_rgb(cr, red, green, blue);
            cairo_rectangle(cr, (double)x, (double)y, (double)(end - x), 1.0);
            cairo_fill(cr);
            x = end;
        }
    }
    return FALSE;
}

static void refresh(simulator_t *simulator) {
    char text[256];
    (void)snprintf(text, sizeof(text),
                   "State: %s    Commands: 0x%08x\n"
                   "%s\n"
                   "Test injection: Y success, X failure, O provisioning "
                   "complete, E eject, K lock, F fault",
                   fv_state_name(simulator->app.state),
                   (unsigned)simulator->last_commands,
                   simulator->persistence_enabled
                       ? "Attempts use persistent development storage"
                       : "C confirms a simulated attempt-counter write");
    gtk_label_set_text(GTK_LABEL(simulator->status), text);
    gtk_widget_queue_draw(simulator->display);
}

static void refresh_input_map(simulator_t *simulator) {
    fv_input_map_t map;
    fv_input_map_for_app(&simulator->app, &map);
    fv_input_controller_set_map(&simulator->input_controller, &map);
}

static void dispatch_event(simulator_t *simulator, fv_event_t event) {
    simulator->last_commands = execute_commands(
        simulator, fv_app_handle(&simulator->app, event));
    refresh_input_map(simulator);
    refresh(simulator);
}

static void emit_input_event(void *context, fv_event_t event) {
    dispatch_event(context, event);
}

static uint32_t monotonic_ms(void) {
    return (uint32_t)((uint64_t)g_get_monotonic_time() / 1000u);
}

static gboolean poll_inputs(gpointer data) {
    simulator_t *simulator = data;
    fv_input_controller_update(&simulator->input_controller,
                               simulator->pressed_inputs, monotonic_ms(),
                               emit_input_event, simulator);
    return G_SOURCE_CONTINUE;
}

static bool key_to_input(guint key, fv_input_id_t *input) {
    switch (key) {
        case GDK_KEY_Up:        *input = FV_INPUT_UP; return true;
        case GDK_KEY_Down:      *input = FV_INPUT_DOWN; return true;
        case GDK_KEY_Left:      *input = FV_INPUT_LEFT; return true;
        case GDK_KEY_Right:     *input = FV_INPUT_RIGHT; return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_space:     *input = FV_INPUT_SELECT; return true;
        case GDK_KEY_BackSpace:
        case GDK_KEY_Escape:    *input = FV_INPUT_BACK; return true;
        default: return false;
    }
}

static bool key_to_system_event(guint key, fv_event_t *event) {
    switch (key) {
        case GDK_KEY_y:
        case GDK_KEY_Y:         *event = FV_EVENT_AUTH_SUCCEEDED; return true;
        case GDK_KEY_x:
        case GDK_KEY_X:         *event = FV_EVENT_AUTH_FAILED; return true;
        case GDK_KEY_c:
        case GDK_KEY_C:         *event = FV_EVENT_ATTEMPT_COUNTER_STORED; return true;
        case GDK_KEY_o:
        case GDK_KEY_O:         *event = FV_EVENT_PROVISIONING_SUCCEEDED; return true;
        case GDK_KEY_e:
        case GDK_KEY_E:         *event = FV_EVENT_USB_EJECTED; return true;
        case GDK_KEY_k:
        case GDK_KEY_K:         *event = FV_EVENT_LOCK_REQUESTED; return true;
        case GDK_KEY_f:
        case GDK_KEY_F:         *event = FV_EVENT_STORAGE_FAILED; return true;
        default: return false;
    }
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *key_event,
                             gpointer data) {
    (void)widget;
    simulator_t *simulator = data;
    fv_input_id_t input;
    if (key_to_input(key_event->keyval, &input)) {
        simulator->pressed_inputs |= FV_INPUT_BIT(input);
        return TRUE;
    }
    fv_event_t event;
    if (!key_to_system_event(key_event->keyval, &event)) {
        return FALSE;
    }
    dispatch_event(simulator, event);
    return TRUE;
}

static gboolean on_key_release(GtkWidget *widget, GdkEventKey *key_event,
                               gpointer data) {
    (void)widget;
    simulator_t *simulator = data;
    fv_input_id_t input;
    if (!key_to_input(key_event->keyval, &input)) return FALSE;
    simulator->pressed_inputs &= ~FV_INPUT_BIT(input);
    return TRUE;
}

int main(int argc, char **argv) {
    bool provisioned = true;
    const char *state_directory = NULL;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--unprovisioned") == 0) {
            provisioned = false;
        } else if (strcmp(argv[index], "--state-dir") == 0 &&
                   index + 1 < argc) {
            state_directory = argv[++index];
        } else {
            fprintf(stderr, "usage: %s [--unprovisioned] "
                    "[--state-dir DIRECTORY]\n", argv[0]);
            return 2;
        }
    }

    gtk_init(&argc, &argv);
    simulator_t simulator = {0};
    uint8_t persisted_attempts = 0u;
    if (state_directory != NULL) {
        if (!fv_host_services_init(&simulator.services,
                                   &simulator.services_context,
                                   state_directory)) {
            fprintf(stderr, "cannot initialise development state directory\n");
            return 1;
        }
        simulator.persistence_enabled = true;
        fv_security_state_t state;
        const fv_persist_result_t load_result =
            simulator.services.ops->load_security_state(&simulator.services,
                                                         &state);
        if (load_result == FV_PERSIST_OK) {
            provisioned = state.provisioned;
            persisted_attempts = state.failed_attempts;
        } else if (load_result == FV_PERSIST_NOT_FOUND && provisioned) {
            fv_device_secret_status_t secret_status;
            if (simulator.services.ops->device_secret_status(
                    &simulator.services, &secret_status) != FV_PERSIST_OK) {
                fprintf(stderr, "cannot inspect development device secret\n");
                return 1;
            }
            if (secret_status == FV_DEVICE_SECRET_EMPTY) {
                fv_device_secret_t secret;
                if (!simulator.services.ops->random_fill(
                        &simulator.services, secret.device_secret,
                        sizeof(secret.device_secret)) ||
                    simulator.services.ops->provision_device_secret(
                        &simulator.services, &secret) != FV_PERSIST_OK) {
                    secure_clear(&secret, sizeof(secret));
                    fprintf(stderr, "cannot create development device secret\n");
                    return 1;
                }
                secure_clear(&secret, sizeof(secret));
            } else if (secret_status != FV_DEVICE_SECRET_ACTIVE) {
                fprintf(stderr, "development device secret is revoked\n");
                return 1;
            }
            state = (fv_security_state_t) {
                .sequence = 1u,
                .failed_attempts = 0u,
                .provisioned = true,
            };
            if (simulator.services.ops->store_security_state(
                    &simulator.services, &state) != FV_PERSIST_OK) {
                secure_clear(&state, sizeof(state));
                fprintf(stderr, "cannot create development security state\n");
                return 1;
            }
        } else if (load_result != FV_PERSIST_NOT_FOUND) {
            fprintf(stderr, "development device state is corrupt\n");
            return 1;
        }
        if (provisioned) {
            fv_device_secret_status_t secret_status;
            if (simulator.services.ops->device_secret_status(
                    &simulator.services, &secret_status) != FV_PERSIST_OK ||
                secret_status == FV_DEVICE_SECRET_EMPTY ||
                secret_status == FV_DEVICE_SECRET_INVALID) {
                fprintf(stderr, "development device secret is unavailable\n");
                return 1;
            }
            if (secret_status == FV_DEVICE_SECRET_REVOKED) {
                persisted_attempts = FV_MAX_UNLOCK_ATTEMPTS;
            }
        }
        secure_clear(&state, sizeof(state));
    }
    fv_app_init(&simulator.app, provisioned, persisted_attempts,
                FV_ENTRY_METHOD_WHEELS);
    simulator.last_commands = execute_commands(
        &simulator, fv_app_handle(&simulator.app, FV_EVENT_BOOT_COMPLETED));
    const fv_input_timing_t input_timing = {
        .debounce_ms = FV_INPUT_DEFAULT_DEBOUNCE_MS,
        .repeat_delay_ms = FV_INPUT_DEFAULT_REPEAT_DELAY_MS,
        .repeat_interval_ms = FV_INPUT_DEFAULT_REPEAT_INTERVAL_MS,
    };
    fv_input_controller_init(&simulator.input_controller, &input_timing, 0u,
                             monotonic_ms());
    refresh_input_map(&simulator);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Fuse Vault display simulator");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 16u);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), layout);

    simulator.display = gtk_drawing_area_new();
    gtk_widget_set_size_request(simulator.display,
                                (int)FV_DISPLAY_WIDTH * DISPLAY_SCALE,
                                (int)FV_DISPLAY_HEIGHT * DISPLAY_SCALE);
    gtk_box_pack_start(GTK_BOX(layout), simulator.display, FALSE, FALSE, 0u);

    simulator.status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(simulator.status), 0.0F);
    gtk_box_pack_start(GTK_BOX(layout), simulator.status, FALSE, FALSE, 0u);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),
                     &simulator);
    g_signal_connect(window, "key-release-event", G_CALLBACK(on_key_release),
                     &simulator);
    g_signal_connect(simulator.display, "draw", G_CALLBACK(draw_display),
                     &simulator);

    refresh(&simulator);
    (void)g_timeout_add(5u, poll_inputs, &simulator);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
