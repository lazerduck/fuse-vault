#include "bringup.h"
#if FV_USB_FIDO
#include "fido_adapter.h"
#endif
#include "device_ui_adapter.h"
#include "fuse_vault/pico_buttons.h"
#if FV_TFT_DISPLAY
#include "fuse_vault/tft_display.h"
#endif
#if FV_DEBUG_SCREEN
#include "ram_debug.h"
#include "startup.h"
#endif
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
static fv_ui ui;
static fv_ui_job mailbox;
atomic_bool fv_ui_maintenance;
static atomic_bool disconnected;
static bool refresh;
static uint64_t startup_until;
static atomic_uint format_done, format_total;
static atomic_uint format_started_ms;
static uint64_t format_redraw;
void fv_device_ui_wake(void) {
#if FV_TFT_DISPLAY
    (void)fv_tft_activity();
#endif
}
void fv_device_ui_format_progress(void *context, uint64_t done, uint64_t total) {
    (void)context;
    if (!done)
        atomic_store(&format_started_ms, (unsigned)(time_us_64() / 1000));
    atomic_store(&format_done, (unsigned)done);
    atomic_store(&format_total, (unsigned)total);
}
void fv_device_ui_disconnect(void) {
    atomic_store(&disconnected, true);
}
void fv_device_ui_refresh(void) {
    refresh = true;
}
void fv_device_ui_media_changed(bool unlocked) {
    if (ui.device.unlocked != unlocked)
        refresh = true;
}
void fv_device_ui_init(void) {
    fv_ui_init(&ui);
    ui.fido_enabled = FV_USB_FIDO;
#if FV_TFT_DISPLAY
    fv_tft_init();
    fv_tft_show_startup();
    startup_until = time_us_64() + 500000;
#endif
    fv_pico_buttons_init();
}
void fv_device_ui_poll(void) {
    if (atomic_exchange(&disconnected, false)) {
        if (!ui.fido_modal)
            fv_ui_cancel(&ui);
        refresh = true;
    }
#if FV_USB_FIDO
    fv_fido_ui_poll(&ui);
#endif
    fv_ui_result r;
    if (queue_try_remove(&ui_responses, &r)) {
        if (!r.unlocked)
            fv_usb_storage_clear_transport();
        fv_ui_complete(&ui, r);
        atomic_store(&fv_ui_maintenance, false);
    }
    if (refresh && !ui.fido_modal
#if FV_USB_FIDO
        && !fv_fido_busy()
#endif
        && ui.screen != UI_WAIT) {
        fv_ui_cancel(&ui);
        refresh = false;
    }
    fv_ui_key key = fv_pico_buttons_poll(ui.fido_generation);
#if FV_TFT_DISPLAY
    if (key && fv_tft_activity())
        key = 0; /* A dark-screen press wakes only; never submits a secret or approval. */
#endif
    if (key && time_us_64() >= startup_until)
        fv_ui_keypress(&ui, key);
    if (ui.screen == UI_WAIT && ui.job.op == UI_CREATE) {
        unsigned total = atomic_load(&format_total);
        uint64_t now = time_us_64();
        if (total && (!format_redraw || now - format_redraw >= 250000)) {
            ui.format_total = total;
            ui.format_done = atomic_load(&format_done);
            ui.format_milliseconds = (uint32_t)(now / 1000) - atomic_load(&format_started_ms);
            fv_ui_render(&ui);
            format_redraw = now;
        }
    }
    fv_ui_animate(&ui, (uint32_t)(time_us_64() / 1000));
    /* USB callbacks and UI polling run on core 0; no cross-core counters. */
    static uint64_t activity_sample;
    uint64_t activity_now = time_us_64();
    if (activity_now - activity_sample >= 500000) {
        uint32_t reads, writes;
        fv_usb_storage_take_activity(&reads, &writes);
        uint64_t elapsed = activity_now - activity_sample;
        activity_sample = activity_now;
        fv_ui_activity(&ui, reads, writes, elapsed);
    }
    if (ui.pending
#if FV_USB_FIDO
        && !fv_fido_busy()
#endif
    ) {
        atomic_store(&format_done, 0);
        atomic_store(&format_total, 0);
        format_redraw = 0;
        fv_command c = {0};
        mailbox = ui.job;
        c.ui = &mailbox;
        if (ui.job.op != UI_STATUS)
            fv_usb_storage_clear_transport();
        atomic_store(&fv_ui_maintenance, true);
        if (queue_try_add(&commands, &c)) {
            ui.pending = false;
            fv_ui_wipe(ui.job.secret, sizeof(ui.job.secret));
            ui.job.length = 0;
            fv_ui_wipe(ui.job.current, 64);
            ui.job.current_length = 0;
            fv_ui_wipe(&ui.entry, sizeof(ui.entry));
        } else
            fv_ui_wipe(&mailbox, sizeof(mailbox));
        fv_ui_wipe(&c, sizeof(c));
    }
#if FV_TFT_DISPLAY
    fv_tft_poll(ui.framebuffer);
#endif
}
bool fv_device_ui_command(const char *command, char *out, size_t size) {
#if FV_DEBUG_SCREEN
    bool startup = !strncmp(command, "STARTUP ", 8);
    unsigned startup_frame = 0;
    if (startup) {
        /* Exact single-digit frame index; this command never changes UI state. */
        if (strlen(command) != 9 || command[8] < '0' || command[8] > '9') {
            snprintf(out, size, "{\"command\":\"screen\",\"ok\":false,\"error\":\"invalid startup frame\"}\n");
            return true;
        }
        startup_frame = (unsigned)(command[8] - '0');
    }
    if (!strcmp(command, "SCREEN") || startup) {
        fv_ram_debug ram = fv_ram_debug_read();
        int n = snprintf(
            out, size,
            "{\"command\":\"screen\",\"ok\":true,\"width\":160,\"height\":80,\"format\":\"rgb332\",\"screen\":%u,\"busy\":%s,\"allow_mounted\":%s,\"input_id\":%u,"
            "\"ram\":{\"total_bytes\":%u,\"fixed_bytes\":%u,\"heap_reserved_bytes\":%u,\"heap_peak_"
            "reserved_bytes\":%u,\"uncommitted_bytes\":%u},\"pixels\":\"",
            (unsigned)ui.screen, (ui.screen == UI_WAIT || ui.fido_done) ? "true" : "false",
            ui.fido_modal ? "true" : "false", (unsigned)ui.fido_generation,
            (unsigned)ram.total_bytes, (unsigned)ram.fixed_bytes, (unsigned)ram.heap_reserved_bytes,
            (unsigned)ram.heap_peak_reserved_bytes, (unsigned)ram.uncommitted_bytes);
        if (n < 0 || (size_t)n + 2 * FV_SCREEN_BYTES + 4 >= size)
            return false;
        const char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < FV_SCREEN_BYTES; i++) {
            uint8_t pixel = startup ? fv_startup_pixel(startup_frame, i) : ui.framebuffer[i];
            out[n++] = hex[pixel >> 4];
            out[n++] = hex[pixel & 15];
        }
        memcpy(out + n, "\"}\n", 4);
        return true;
    }
    if (!strncmp(command, "KEY ", 4)) {
        unsigned generation = 0;
        int offset = 0;
        const char *key = command + 4;
        if (sscanf(key, "%u %n", &generation, &offset) == 1 && offset)
            key += offset;
        const char *keys[] = {"UP", "DOWN", "LEFT", "RIGHT", "SELECT", "BACK"};
        unsigned i;
        for (i = 0; i < 6; i++)
            if (!strcmp(key, keys[i]))
                break;
        bool accepted = time_us_64() >= startup_until && i < 6 && ui.screen != UI_WAIT && !ui.fido_done &&
                        (!offset || generation == ui.fido_generation) && (!ui.fido_modal || offset);
        if (accepted) {
            fv_device_ui_wake();
            fv_ui_keypress(&ui, (fv_ui_key)(i + 1));
        }
        snprintf(out, size, "{\"command\":\"key\",\"ok\":true,\"accepted\":%s}\n",
                 accepted ? "true" : "false");
        return true;
    }
#endif
    (void)command;
    (void)out;
    (void)size;
    return false;
}
