#include "scene.h"
#include "drawing.h"
#include <stdio.h>
#include <string.h>

static void error_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_SELECT || key == UI_BACK)
        ui_submit(u, UI_STATUS);
}

static void busy_visual(fv_ui *u, const char *title) {
    ui_draw_vault(u, 68, 17, UI_COLOUR_ACCENT);
    /* Travelling lights indicate activity, never estimated progress. */
    for (unsigned i = 0; i < 12; ++i) {
        unsigned age = (u->busy_frame + 12 - i) % 12;
        fv_ui_colour colour = age == 0 ? UI_COLOUR_TEXT :
                              age < 3 ? UI_COLOUR_ACCENT : 0x05;
        unsigned x = 27 + i*9;
        for (unsigned dy = 0; dy < 3; ++dy)
            for (unsigned dx = 0; dx < 6; ++dx)
                ui_draw_pixel(u, x+dx, 57+dy, colour);
    }
    ui_draw_text_at(u, (160 - (unsigned)strlen(title)*6)/2, 46, title, 1, UI_COLOUR_TEXT);
}

static void render_wait(fv_ui *u) {
    char b[40];
    ui_draw_header(u, "FUSE VAULT", "BUSY");
    if (u->job.op == UI_CREATE && u->format_total && u->format_done >= u->format_total) {
        busy_visual(u, "PREPARING USB DRIVE");
    } else if (u->job.op == UI_CREATE && u->format_total) {
        uint32_t done = u->format_done > u->format_total ? u->format_total : u->format_done;
        unsigned percent = (unsigned)((uint64_t)done * 100 / u->format_total);
        ui_draw_text_at(u, 4, 16, "PREPARING STORAGE", 1, UI_COLOUR_ACCENT);
        if (u->format_total < 2048)
            snprintf(b, sizeof(b), "%u%%  %lu / %lu KIB", percent, (unsigned long)(done / 2),
                     (unsigned long)((u->format_total + 1) / 2));
        else
            snprintf(b, sizeof(b), "%u%%  %lu / %lu MIB", percent, (unsigned long)(done / 2048),
                     (unsigned long)(u->format_total / 2048));
        ui_draw_text_at(u, 4, 25, b, 1, UI_COLOUR_TEXT);
        unsigned filled = (unsigned)((uint64_t)done * 154 / u->format_total);
        for (unsigned y = 36; y < 44; y++)
            for (unsigned x = 2; x < 158; x++)
                if (y == 36 || y == 43 || x == 2 || x == 157 || x < 3 + filled) {
                    ui_draw_pixel(u, x, y, UI_COLOUR_SUCCESS);
                }
        unsigned rate =
            u->format_milliseconds ? (unsigned)((uint64_t)done * 5000 / u->format_milliseconds) : 0;
        snprintf(b, sizeof(b), "%u.%u KIB/S  %lus", rate / 10, rate % 10,
                 (unsigned long)(u->format_milliseconds / 1000));
        ui_draw_line(u, 5, b);
    } else {
        const char *activity = "READING DEVICE STATUS";
        switch (u->job.op) {
        case UI_CREATE:
            activity = "CREATING VAULT";
            break;
        case UI_UNLOCK:
            activity = "UNLOCKING VAULT";
            break;
        case UI_LOCK:
            activity = "LOCKING VAULT";
            break;
        case UI_POLICY:
        case UI_FIDO_POLICY:
            activity = "SAVING SETTINGS";
            break;
        case UI_ERASE:
            activity = "DESTROYING VAULT";
            break;
        case UI_CHANGE:
            activity = "CHANGING CREDENTIAL";
            break;
        case UI_FIDO_INIT:
            activity = "RESETTING FIDO";
            break;
        case UI_PASSKEY_LIST:
            activity = "LOADING PASSKEYS";
            break;
        case UI_PASSKEY_DELETE:
            activity = "DELETING PASSKEY";
            break;
        default:
            break;
        }
        busy_visual(u, activity);
    }
    ui_draw_footer(u, "KEEP POWER CONNECTED", NULL);
}

static void render_error(fv_ui *u) {
    char b[40];
    ui_draw_header(u, "OPERATION FAILED", NULL);
    const char *reason = "PLEASE CHECK DEVICE";
    const char *next = "RETURN TO CHECK STATUS";
    switch (u->error) {
    case -1: reason = "REQUEST NOT VALID"; break;
    case -2: reason = "DEVICE NOT READY"; break;
    case -3: reason = "STORAGE ERROR"; next = "CHECK SD CONNECTION"; break;
    case -4: reason = "VERIFICATION FAILED"; break;
    case -5: reason = "ACCESS DENIED"; break;
    case -6: reason = "LEGACY VAULT"; next = "USE LEGACY DEBUG UNLOCK"; break;
    default: break;
    }
    ui_draw_text_at(u, 4, 21, reason, 1, UI_COLOUR_WARNING);
    ui_draw_text_at(u, 4, 37, next, 1, UI_COLOUR_TEXT);
    snprintf(b, sizeof(b), "DETAIL %d", u->error);
    ui_draw_text_at(u, 4, 53, b, 1, UI_COLOUR_MUTED);
    ui_draw_footer(u, "\005 BACK", "\006 RETURN");
}

const ui_scene ui_wait_scene = {.render = render_wait};
const ui_scene ui_error_scene = {.key = error_key, .render = render_error};
