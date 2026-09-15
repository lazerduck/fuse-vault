#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/input.h"
#include "fuse_vault/ui.h"
#include "fuse_vault/virtual_msc.h"
#include "host_services.h"
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
#include "simulator_fido.h"
#include <signal.h>
#include <sys/wait.h>
#endif

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

enum { DISPLAY_SCALE = 4 };
typedef struct {
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    fv_simulator_fido_t fido;
    bool host_busy, cancel_host, close_pending, lock_pending;
    int approval;
    GtkWidget *fido_panel, *rp, *account, *fido_result;
    char host_result[512];
    bool host_success;
#endif
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    fv_simulator_fido_close(&s->fido);
#endif
    bool ok = true;
    if (s->msc.attached) {
        ok = fv_virtual_msc_synchronize_cache(&s->msc) == FV_MSC_OK;
    }
    fv_virtual_msc_block_requests(&s->msc);
    fv_virtual_msc_detach(&s->msc);
    return ok;
}
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
static int fido_presence(void *context, bool reset);
static uint32_t fido_clock(void *context) {
    (void)context;
    return (uint32_t)((uint64_t)g_get_monotonic_time() / 1000u);
}
static bool attach_fido(void *context, const fv_volume_master_key_t *vmk,
    const fv_media_layout_t *layout, const fv_encryption_stack_descriptor_t *stack) {
    return fv_simulator_fido_attach(&((simulator_t *)context)->fido, vmk, layout, stack);
}
static bool manage_fido(void *context, fv_passkey_action_t action,
    uint16_t *index, uint16_t *count, fv_passkey_t *entry) {
    return fv_simulator_fido_manage(&((simulator_t *)context)->fido, action, index, count, entry);
}
#endif
static const fv_runtime_usb_ops_t USB_OPS = {
    .attach_msc = attach, .detach_usb = detach,
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    .attach_fido = attach_fido, .manage_passkeys = manage_fido
#endif
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
    /* Preserve older 128 KiB storage-only images exactly as they are. New
     * devices reserve enough encrypted media for both FIDO snapshot banks. */
    char *media_path = g_build_filename(s->directory, "vault-media.bin", NULL);
    struct stat media_stat;
    bool legacy = stat(media_path, &media_stat) == 0 && media_stat.st_size == 256 * FV_BLOCK_SIZE;
    g_free(media_path);
    if (!fv_host_services_init_sized(&s->services, &s->services_context,
        s->directory, legacy ? 256u : 4096u,
        legacy ? 16u : FV_MEDIA_DEFAULT_FIDO_BLOCKS)) return false;
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    fv_app_set_fido_available(&s->app, !legacy);
    g_strlcpy(s->host_result, legacy
        ? "This older device has no room for FIDO. New device creates a separate FIDO-capable device."
        : "Unlock and select FIDO2. OK opens saved passkeys when idle.", sizeof(s->host_result));
    fv_simulator_fido_init(&s->fido, &s->runtime, fido_clock, fido_presence, s);
#endif
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    if (s->fido_panel) gtk_widget_set_sensitive(s->fido_panel,
        s->fido.attached && s->app.state == FV_STATE_FIDO_READY && !s->host_busy);
    if (s->fido_result) gtk_label_set_text(GTK_LABEL(s->fido_result), s->host_result);
#endif
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    if (s->host_busy) {
        if (event == FV_EVENT_LOCK_REQUESTED || event == FV_EVENT_USB_EJECTED) {
            s->cancel_host = true; s->lock_pending = true;
        } else if (s->app.fido_waiting && event == FV_EVENT_SELECT) s->approval = 0;
        else if (event == FV_EVENT_BACK) { s->approval = 2; s->cancel_host = true; }
        return;
    }
    bool was_unlocked = s->app.session_unlocked;
#endif
    fv_device_runtime_handle_event(&s->runtime, event);
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    fv_simulator_fido_session(&s->fido, was_unlocked);
#endif
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
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(s->window));
    if (GTK_IS_ENTRY(focus)) return FALSE;
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    if (s->host_busy && (strcmp(name, "Restart") == 0 || strcmp(name, "New device") == 0)) return;
#endif
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

#ifdef FUSE_VAULT_SIMULATOR_FIDO2
/* The engine is synchronous. Pump the GUI only while guarded against runtime
 * reentry: lock and window closure are deferred until its stack unwinds. */
static void host_pump(void) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000u);
}
static int fido_presence(void *context, bool reset) {
    simulator_t *s = context;
    s->approval = -1;
    s->app.fido_waiting = true;
    s->app.fido_reset_pending = reset;
    refresh(s);
    uint32_t start = monotonic_ms();
    while (s->approval < 0 && !s->cancel_host &&
        (uint32_t)(monotonic_ms() - start) < 30000u) host_pump();
    int result = s->cancel_host ? 2 : s->approval < 0 ? 1 : s->approval;
    s->app.fido_waiting = s->app.fido_reset_pending = false;
    refresh(s);
    return result;
}
static bool host_line(simulator_t *s, const char *line, int output) {
    if (g_str_has_prefix(line, "DONE ") || g_str_has_prefix(line, "ERROR ")) {
        s->host_success = g_str_has_prefix(line, "DONE ");
        g_strlcpy(s->host_result, line + (s->host_success ? 5 : 6), sizeof(s->host_result));
        return true;
    }
    if (!g_str_has_prefix(line, "CTAP ")) return false;
    const char *hex = line + 5;
    size_t length = strlen(hex);
    uint8_t request[2048], response[4096];
    if (length == 0 || length % 2 || length / 2 > sizeof(request)) return false;
    for (size_t i = 0; i < length / 2; ++i) {
        int a = g_ascii_xdigit_value(hex[2*i]), b = g_ascii_xdigit_value(hex[2*i+1]);
        if (a < 0 || b < 0) return false;
        request[i] = (uint8_t)(a * 16 + b);
    }
    size_t count = fv_simulator_fido_command(&s->fido, request, length / 2, response, sizeof(response));
    if (!count || s->fido.failed) return false;
    char encoded[8193];
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        encoded[2*i] = digits[response[i] >> 4];
        encoded[2*i+1] = digits[response[i] & 15];
    }
    encoded[2*count] = '\n';
    size_t sent = 0;
    while (sent < 2*count+1) {
        ssize_t n = write(output, encoded + sent, 2*count+1-sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}
static void run_fido_host(simulator_t *s, const char *operation, const char *rp, const char *account) {
    if (s->host_busy || !s->fido.attached || s->app.state != FV_STATE_FIDO_READY) return;
    s->host_busy = true; s->cancel_host = false; s->host_success = false;
    g_strlcpy(s->host_result, "Waiting for device approval: press OK when prompted.", sizeof(s->host_result));
    refresh(s);
    gchar *argv[] = {FUSE_VAULT_FIDO_PYTHON, FUSE_VAULT_FIDO_CLIENT,
        s->directory, (char *)operation, (char *)rp, (char *)account, NULL};
    GPid pid = 0;
    int input = -1, output = -1;
    GError *error = NULL;
    void (*old_pipe)(int) = signal(SIGPIPE, SIG_IGN);
    if (g_spawn_async_with_pipes(NULL, argv, NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
        &pid, &output, &input, NULL, &error)) {
        (void)fcntl(input, F_SETFL, O_NONBLOCK);
        char line[8192]; size_t used = 0;
        uint32_t start = monotonic_ms();
        bool done = false, valid = true;
        while (!done && valid && !s->cancel_host && (uint32_t)(monotonic_ms()-start) < 45000u) {
            char c;
            ssize_t n = read(input, &c, 1);
            if (n == 0) break;
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
                host_pump(); continue;
            }
            if (c == '\n') {
                line[used] = 0;
                done = g_str_has_prefix(line, "DONE ") || g_str_has_prefix(line, "ERROR ");
                valid = host_line(s, line, output); used = 0;
            } else if (used + 1 < sizeof(line)) line[used++] = c;
            else valid = false;
        }
        close(input); close(output);
        if (!done || !valid || s->cancel_host) {
            s->host_success = false;
            g_strlcpy(s->host_result, s->cancel_host ? "FIDO request cancelled." : "FIDO host request failed or timed out.", sizeof(s->host_result));
        }
        /* No child or pipe callbacks survive this operation. */
        (void)kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        g_spawn_close_pid(pid);
    } else {
        g_strlcpy(s->host_result, error->message, sizeof(s->host_result));
        g_clear_error(&error);
    }
    signal(SIGPIPE, old_pipe);
    s->host_busy = false;
    if (s->fido.failed) dispatch_event(s, FV_EVENT_FATAL_ERROR);
    if (s->lock_pending) { s->lock_pending = false; dispatch_event(s, FV_EVENT_LOCK_REQUESTED); }
    refresh(s);
    if (s->close_pending && s->window) gtk_widget_destroy(s->window);
}
static void fido_action(GtkButton *button, gpointer context) {
    simulator_t *s = context;
    char *rp = g_strdup(gtk_entry_get_text(GTK_ENTRY(s->rp)));
    char *account = g_strdup(gtk_entry_get_text(GTK_ENTRY(s->account)));
    gtk_widget_grab_focus(s->display);
    run_fido_host(s, g_object_get_data(G_OBJECT(button), "operation"), rp, account);
    g_free(rp); g_free(account);
}
static gboolean close_window(GtkWidget *widget, GdkEvent *event, gpointer context) {
    (void)widget; (void)event;
    simulator_t *s = context;
    if (!s->host_busy) return FALSE;
    s->cancel_host = s->close_pending = true;
    return TRUE;
}
#endif

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
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 850);
    gtk_container_add(GTK_CONTAINER(s.window), scroll);
    gtk_container_add(GTK_CONTAINER(scroll), layout);
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
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    gtk_box_pack_start(GTK_BOX(layout), gtk_label_new(
        "Simulated FIDO host — register/authenticate locally (no browser USB device)"), FALSE, FALSE, 0u);
    s.fido_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    s.rp = gtk_entry_new(); s.account = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(s.rp), "example.com");
    gtk_entry_set_text(GTK_ENTRY(s.account), "alice@example.com");
    gtk_widget_set_tooltip_text(s.rp, "Relying party / site");
    gtk_widget_set_tooltip_text(s.account, "Account");
    gtk_box_pack_start(GTK_BOX(s.fido_panel), s.rp, TRUE, TRUE, 0u);
    gtk_box_pack_start(GTK_BOX(s.fido_panel), s.account, TRUE, TRUE, 0u);
    for (unsigned i = 0; i < 2; ++i) {
        GtkWidget *b = gtk_button_new_with_label(i == 0 ? "Register" : "Authenticate");
        g_object_set_data(G_OBJECT(b), "operation", i == 0 ? "register" : "authenticate");
        g_signal_connect(b, "clicked", G_CALLBACK(fido_action), &s);
        gtk_box_pack_start(GTK_BOX(s.fido_panel), b, FALSE, FALSE, 0u);
    }
    gtk_box_pack_start(GTK_BOX(layout), s.fido_panel, FALSE, FALSE, 0u);
    s.fido_result = gtk_label_new("Unlock and select FIDO2. OK opens saved passkeys when idle.");
    gtk_label_set_line_wrap(GTK_LABEL(s.fido_result), TRUE);
    gtk_box_pack_start(GTK_BOX(layout), s.fido_result, FALSE, FALSE, 0u);
    g_signal_connect(s.window, "delete-event", G_CALLBACK(close_window), &s);
#endif
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
