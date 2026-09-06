#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/input.h"
#include "fuse_vault/ui.h"
#include "fuse_vault/virtual_msc.h"
#include "host_services.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

enum { DISPLAY_SCALE = 4 };
typedef struct {
    fv_app_t app;
    fv_platform_services_t services;
    fv_host_services_context_t services_context;
    fv_device_runtime_t runtime;
    fv_virtual_msc_t msc;
    fv_input_controller_t input_controller;
    uint32_t keyboard_inputs;
    uint32_t mouse_inputs;
    bool started;
    int lock_fd;
    char *directory;
    GtkWidget *window, *display, *status, *note, *storage;
} simulator_t;

static void clear(void *data, size_t length) {
    volatile uint8_t *p = data;
    while (length-- != 0u) *p++ = 0u;
}
static bool attach(void *context, fv_block_device_t *blocks) {
    return fv_virtual_msc_attach(&((simulator_t *)context)->msc, blocks);
}
static bool detach(void *context) {
    simulator_t *s = context;
    bool ok = true;
    if (s->msc.attached) {
        ok = fv_virtual_msc_synchronize_cache(&s->msc) == FV_MSC_OK;
    }
    fv_virtual_msc_block_requests(&s->msc);
    fv_virtual_msc_detach(&s->msc);
    return ok;
}
static const fv_runtime_usb_ops_t USB_OPS = {
    .attach_msc = attach, .detach_usb = detach, .attach_fido = NULL
};
static uint32_t monotonic_ms(void) {
    return (uint32_t)((uint64_t)g_get_monotonic_time() / 1000u);
}
static void refresh_input_map(simulator_t *s) {
    fv_input_map_t map;
    fv_input_map_for_app(&s->app, &map);
    fv_input_controller_set_map(&s->input_controller, &map);
}
static bool present_settings_work(void *context, const fv_app_t *app);
static bool boot(simulator_t *s) {
    if (s->started) fv_device_runtime_shutdown(&s->runtime);
    s->started = false;
    s->keyboard_inputs = s->mouse_inputs = 0u;
    fv_virtual_msc_init(&s->msc);
    if (!fv_host_services_init(&s->services, &s->services_context,
                               s->directory)) return false;
    fv_security_state_t state = {0};
    fv_device_secret_status_t roots;
    const fv_persist_result_t loaded =
        s->services.ops->load_security_state(&s->services, &state);
    bool safe = loaded == FV_PERSIST_OK || loaded == FV_PERSIST_NOT_FOUND;
    safe = safe && s->services.ops->device_secret_status(
        &s->services, &roots) == FV_PERSIST_OK;
    fv_entry_method_t method = FV_ENTRY_METHOD_WHEELS;
    if (safe && roots == FV_DEVICE_SECRET_REVOKED) {
        state.provisioned = true;
        state.failed_attempts = FV_MAX_UNLOCK_ATTEMPTS;
    } else if (safe && state.provisioned) {
        safe = roots == FV_DEVICE_SECRET_ACTIVE &&
            fv_boot_recover_entry_method(&s->services, &method) ==
                FV_BOOT_RECOVERY_OK;
    } else if (safe) {
        safe = roots == FV_DEVICE_SECRET_EMPTY ||
               roots == FV_DEVICE_SECRET_ACTIVE;
    }
    fv_app_init(&s->app, state.provisioned, state.failed_attempts, method);
    clear(&state, sizeof(state));
    const fv_credential_costs_t costs = {
        .pbkdf2_iterations = 100000u, .kmac_iterations = 10000u
    };
    if (!fv_device_runtime_init(&s->runtime, &s->app, &s->services,
            &s->services_context.vault_device, &USB_OPS, s, &costs)) return false;
    fv_device_runtime_set_present(&s->runtime, present_settings_work, s);
    s->started = true;
    fv_device_runtime_handle_event(&s->runtime,
        safe ? FV_EVENT_BOOT_COMPLETED : FV_EVENT_FATAL_ERROR);
    const fv_input_timing_t timing = {
        FV_INPUT_DEFAULT_DEBOUNCE_MS, FV_INPUT_DEFAULT_REPEAT_DELAY_MS,
        FV_INPUT_DEFAULT_REPEAT_INTERVAL_MS
    };
    fv_input_controller_init(&s->input_controller, &timing, 0u, monotonic_ms());
    refresh_input_map(s);
    return true;
}
static bool select_directory(simulator_t *s, const char *directory) {
    if (g_mkdir_with_parents(directory, 0700) != 0) return false;
    char *path = g_build_filename(directory, "simulator.lock", NULL);
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    g_free(path);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return false; }
    if (s->started) {
        fv_device_runtime_shutdown(&s->runtime);
        s->started = false;
    }
    if (s->lock_fd >= 0) close(s->lock_fd);
    s->lock_fd = fd;
    g_free(s->directory);
    s->directory = g_strdup(directory);
    return boot(s);
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


static void refresh(simulator_t *s) {
    if (s->status == NULL) return;
    char *text = g_strdup_printf("%s  ·  USB storage %s\nDevice: %s",
        fv_state_name(s->app.state), s->msc.attached ? "connected" : "locked",
        s->directory);
    gtk_label_set_text(GTK_LABEL(s->status), text);
    g_free(text);
    gtk_widget_set_sensitive(s->storage, s->msc.attached);
    if (!s->msc.attached) gtk_entry_set_text(GTK_ENTRY(s->note), "");
    gtk_widget_queue_draw(s->display);
}
static bool present_settings_work(void *context, const fv_app_t *app) {
    simulator_t *s = context;
    (void)app;
    /* Draw the busy screen synchronously without pumping input or allowing a
     * restart callback to re-enter a credential transaction. Headless tests
     * have no window; the same view is still exercised by the renderer tests. */
    if (!s->display || !gtk_widget_get_realized(s->display)) return true;
    refresh(s);
    GdkWindow *window = gtk_widget_get_window(s->display);
    cairo_region_t *region = gdk_window_get_visible_region(window);
    GdkDrawingContext *drawing = gdk_window_begin_draw_frame(window, region);
    gtk_widget_draw(s->display, gdk_drawing_context_get_cairo_context(drawing));
    gdk_window_end_draw_frame(window, drawing);
    cairo_region_destroy(region);
    gdk_display_flush(gdk_window_get_display(window));
    return true;
}

static void dispatch_event(simulator_t *s, fv_event_t event) {
    fv_device_runtime_handle_event(&s->runtime, event);
    refresh_input_map(s);
    refresh(s);
}
static void emit_input_event(void *context, fv_event_t event) {
    dispatch_event(context, event);
}
static gboolean poll_inputs(gpointer data) {
    simulator_t *s = data;
    fv_input_controller_update(&s->input_controller,
        s->keyboard_inputs | s->mouse_inputs, monotonic_ms(), emit_input_event, s);
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


static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    simulator_t *s = data;
    if (gtk_window_get_focus(GTK_WINDOW(s->window)) == s->note) return FALSE;
    fv_input_id_t input;
    if (!key_to_input(event->keyval, &input)) return FALSE;
    s->keyboard_inputs |= FV_INPUT_BIT(input);
    return TRUE;
}
static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    simulator_t *s = data;
    fv_input_id_t input;
    if (!key_to_input(event->keyval, &input)) return FALSE;
    s->keyboard_inputs &= ~FV_INPUT_BIT(input);
    return gtk_window_get_focus(GTK_WINDOW(s->window)) != s->note;
}
static gboolean focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget; (void)event;
    simulator_t *s = data;
    s->keyboard_inputs = s->mouse_inputs = 0u;
    return FALSE;
}
static void control_press(GtkButton *button, gpointer data) {
    simulator_t *s = data;
    unsigned input = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "input"));
    gtk_widget_grab_focus(s->display);
    s->mouse_inputs |= FV_INPUT_BIT(input);
}
static void control_release(GtkButton *button, gpointer data) {
    simulator_t *s = data;
    unsigned input = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "input"));
    s->mouse_inputs &= ~FV_INPUT_BIT(input);
}
static void action(GtkButton *button, gpointer data) {
    simulator_t *s = data;
    const char *name = g_object_get_data(G_OBJECT(button), "action");
    if (strcmp(name, "Restart") == 0) {
        if (!boot(s)) dispatch_event(s, FV_EVENT_FATAL_ERROR);
    } else if (strcmp(name, "New device") == 0) {
        char *parent = g_path_get_dirname(s->directory);
        char *id = g_uuid_string_random();
        char *path = g_build_filename(parent, id, NULL);
        if (!select_directory(s, path)) {
            gtk_label_set_text(GTK_LABEL(s->status), "Could not create a new device.");
            g_free(parent); g_free(id); g_free(path); return;
        }
        g_free(parent); g_free(id); g_free(path);
    } else {
        dispatch_event(s, strcmp(name, "Eject") == 0
            ? FV_EVENT_USB_EJECTED : FV_EVENT_LOCK_REQUESTED);
    }
    gtk_widget_grab_focus(s->display);
    refresh(s);
}
static void storage_action(GtkButton *button, gpointer data) {
    simulator_t *s = data;
    bool save = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "save")) != 0;
    uint8_t block[FV_BLOCK_SIZE] = {0};
    fv_msc_result_t result;
    if (save) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(s->note));
        size_t length = strlen(text);
        if (length > FV_BLOCK_SIZE - 1u) {
            gtk_label_set_text(GTK_LABEL(s->status), "Sample note exceeds 511 UTF-8 bytes.");
            return;
        }
        memcpy(block, text, length);
        result = fv_virtual_msc_write10(&s->msc, 0u, 1u, block);
        if (result == FV_MSC_OK) result = fv_virtual_msc_synchronize_cache(&s->msc);
    } else {
        result = fv_virtual_msc_read10(&s->msc, 0u, 1u, block);
        block[FV_BLOCK_SIZE - 1u] = 0u;
        if (result == FV_MSC_OK) {
            char *valid = g_utf8_make_valid((const char *)block, -1);
            gtk_entry_set_text(GTK_ENTRY(s->note), valid);
            g_free(valid);
        }
    }
    clear(block, sizeof(block));
    if (result != FV_MSC_OK) {
        dispatch_event(s, FV_EVENT_STORAGE_FAILED);
    } else {
        gtk_label_set_text(GTK_LABEL(s->status), save
            ? "Sample note encrypted and saved. Restart, unlock, then Load to recover it."
            : "Sample note read through the decrypted storage interface.");
    }
}
int main(int argc, char **argv) {
    const char *directory = NULL;
    bool fresh = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) directory = argv[++i];
        else if (strcmp(argv[i], "--unprovisioned") == 0 || strcmp(argv[i], "--new") == 0)
            fresh = true;
        else {
            fprintf(stderr, "usage: %s [--state-dir DIRECTORY] [--new]\n", argv[0]);
            return 2;
        }
    }
    gtk_init(&argc, &argv);
    simulator_t s = {.lock_fd = -1};
    char *base = g_build_filename(g_get_user_data_dir(), "fuse-vault", "simulator", NULL);
    char *id = fresh && directory == NULL ? g_uuid_string_random() : g_strdup("default");
    char *default_path = g_build_filename(base, id, NULL);
    bool ok = select_directory(&s, directory != NULL ? directory : default_path);
    g_free(base); g_free(id); g_free(default_path);
    if (!ok) {
        fprintf(stderr, "Cannot open simulator device: directory must be private (0700), "
                        "writable, and not open in another simulator.\n");
        if (s.lock_fd >= 0) close(s.lock_fd);
        g_free(s.directory);
        return 1;
    }
    s.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(s.window), "Fuse Vault — local device");
    gtk_container_set_border_width(GTK_CONTAINER(s.window), 18u);
    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(s.window), layout);
    s.display = gtk_drawing_area_new();
    gtk_widget_set_can_focus(s.display, TRUE);
    gtk_widget_set_halign(s.display, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(s.display, FV_DISPLAY_WIDTH * DISPLAY_SCALE,
                                FV_DISPLAY_HEIGHT * DISPLAY_SCALE);
    gtk_box_pack_start(GTK_BOX(layout), s.display, FALSE, FALSE, 0u);
    GtkWidget *hint = gtk_label_new("Arrow keys: navigate   ·   Enter: OK   ·   Esc: Back");
    gtk_box_pack_start(GTK_BOX(layout), hint, FALSE, FALSE, 0u);
    GtkWidget *grid = gtk_grid_new();
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4u);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4u);
    const char *labels[] = {"↑", "↓", "←", "→", "OK", "Back"};
    const int columns[] = {1, 1, 0, 2, 1, 3};
    const int rows[] = {0, 2, 1, 1, 1, 1};
    for (unsigned i = 0u; i < FV_INPUT_COUNT; ++i) {
        GtkWidget *b = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_can_focus(b, FALSE);
        gtk_widget_set_size_request(b, 72, 40);
        g_object_set_data(G_OBJECT(b), "input", GUINT_TO_POINTER(i));
        g_signal_connect(b, "pressed", G_CALLBACK(control_press), &s);
        g_signal_connect(b, "released", G_CALLBACK(control_release), &s);
        gtk_grid_attach(GTK_GRID(grid), b, columns[i], rows[i], 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(layout), grid, FALSE, FALSE, 0u);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *names[] = {"Restart", "New device", "Lock", "Eject"};
    for (unsigned i = 0u; i < 4u; ++i) {
        GtkWidget *b = gtk_button_new_with_label(names[i]);
        gtk_widget_set_can_focus(b, FALSE);
        g_object_set_data(G_OBJECT(b), "action", (gpointer)names[i]);
        g_signal_connect(b, "clicked", G_CALLBACK(action), &s);
        gtk_box_pack_start(GTK_BOX(actions), b, TRUE, TRUE, 0u);
    }
    gtk_box_pack_start(GTK_BOX(layout), actions, FALSE, FALSE, 0u);
    GtkWidget *caption = gtk_label_new(
        "Simulated USB host — sample note (one sector, no desktop drive mount)");
    gtk_box_pack_start(GTK_BOX(layout), caption, FALSE, FALSE, 0u);
    s.storage = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    s.note = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s.note), "Unlock, type a sample note, then Save");
    gtk_box_pack_start(GTK_BOX(s.storage), s.note, TRUE, TRUE, 0u);
    for (unsigned i = 0u; i < 2u; ++i) {
        GtkWidget *b = gtk_button_new_with_label(i == 0u ? "Save" : "Load");
        g_object_set_data(G_OBJECT(b), "save", GINT_TO_POINTER(i == 0u));
        g_signal_connect(b, "clicked", G_CALLBACK(storage_action), &s);
        gtk_box_pack_start(GTK_BOX(s.storage), b, FALSE, FALSE, 0u);
    }
    gtk_box_pack_start(GTK_BOX(layout), s.storage, FALSE, FALSE, 0u);
    s.status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(s.status), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(s.status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(s.status), 85);
    gtk_box_pack_start(GTK_BOX(layout), s.status, FALSE, FALSE, 0u);
    g_signal_connect(s.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(s.window, "key-press-event", G_CALLBACK(on_key_press), &s);
    g_signal_connect(s.window, "key-release-event", G_CALLBACK(on_key_release), &s);
    g_signal_connect(s.window, "focus-out-event", G_CALLBACK(focus_out), &s);
    g_signal_connect(s.display, "draw", G_CALLBACK(draw_display), &s);
    refresh(&s);
    guint timer = g_timeout_add(5u, poll_inputs, &s);
    gtk_widget_show_all(s.window);
    gtk_widget_grab_focus(s.display);
    gtk_main();
    g_source_remove(timer);
    fv_device_runtime_shutdown(&s.runtime);
    close(s.lock_fd);
    g_free(s.directory);
    return 0;
}
