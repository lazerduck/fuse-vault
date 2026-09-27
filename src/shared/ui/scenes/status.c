#include "scene.h"
#include "drawing.h"
#include <stdio.h>

static void error_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_SELECT || key == UI_BACK)
        ui_submit(u, UI_STATUS);
}

static void render_wait(fv_ui *u) {
    char b[40];
    ui_draw_line(u, 0, "FUSE VAULT");
    if (u->job.op == UI_CREATE && u->format_total) {
        uint32_t done = u->format_done > u->format_total ? u->format_total : u->format_done;
        unsigned percent = (unsigned)((uint64_t)done * 100 / u->format_total);
        ui_draw_line(u, 1, done == u->format_total ? "FINALIZING VAULT" : "INITIALIZING BITMAP");
        if (u->format_total < 2048)
            snprintf(b, sizeof(b), "%u%%  %lu / %lu KIB", percent, (unsigned long)(done / 2),
                     (unsigned long)((u->format_total + 1) / 2));
        else
            snprintf(b, sizeof(b), "%u%%  %lu / %lu MIB", percent, (unsigned long)(done / 2048),
                     (unsigned long)(u->format_total / 2048));
        ui_draw_line(u, 2, b);
        unsigned filled = (unsigned)((uint64_t)done * 154 / u->format_total);
        for (unsigned y = 32; y < 40; y++)
            for (unsigned x = 2; x < 158; x++)
                if (y == 32 || y == 39 || x == 2 || x == 157 || x < 3 + filled) {
                    unsigned bit = y * 160 + x;
                    u->framebuffer[bit / 8] |= 0x80u >> (bit % 8);
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
            activity = "INITIALIZING FIDO";
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
        ui_draw_line(u, 2, activity);
        ui_draw_line(u, 4, "PLEASE WAIT");
    }
    ui_draw_line(u, 7, "KEEP POWER CONNECTED");
}

static void render_error(fv_ui *u) {
    char b[40];
    ui_draw_header(u, "OPERATION FAILED", NULL);
    snprintf(b, sizeof(b), "RESULT %d", u->error);
    ui_draw_text_at(u, 4, 22, b, 1, true);
    ui_draw_text_at(u, 4, 42, u->error == -6 ? "USE LEGACY DEBUG UNLOCK" : "CHECK CREDENTIAL OR SD",
                    1, true);
    ui_draw_footer(u, "\005 BACK", "\006 RETURN");
}

const ui_scene ui_wait_scene = {.render = render_wait};
const ui_scene ui_error_scene = {.key = error_key, .render = render_error};
