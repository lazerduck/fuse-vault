#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include <stdio.h>
#include <string.h>

static void lock_vault(fv_ui *u) {
    ui_submit(u, UI_LOCK);
}
static void open_settings(fv_ui *u) {
    ui_scene_show(u, UI_SETTINGS);
}
static const ui_menu_item home_items[] = {
    {"LOCK VAULT", lock_vault},
    {"SETTINGS", open_settings},
};
static const ui_menu home_menu = {home_items, 2, 43, 12, 9, 1, 2};

static void begin_vault_flow(fv_ui *u, fv_access_mode mode) {
    memset(&u->job, 0, sizeof(u->job));
    u->error = 0;
    u->job.count = 1;
    u->job.algorithms[0] = 1;
    u->job.attempts = 10;
    u->job.action = 1;
    u->job.auto_lock_seconds = 300;
    u->job.profile = u->device.profile ? u->device.profile : 2;
    if (u->device.status == 2) {
        u->flow = UI_FLOW_UNLOCK;
        u->job.mode = mode;
        if (u->job.profile == 1) {
            u->error = -6;
            ui_scene_show(u, UI_ERROR);
        } else {
            ui_scene_show(u, UI_SECRET);
        }
    } else {
        u->flow = UI_FLOW_SETUP;
        ui_scene_show(u, UI_METHOD);
    }
}

static void home_key(fv_ui *u, fv_ui_key key) {
    /* Orientation can change only here, never halfway through a credential. */
    if (key == UI_LEFT || key == UI_RIGHT) {
        u->flipped = !u->flipped;
    } else if (key == UI_BACK) {
        if (u->device.unlocked)
            ui_scene_show(u, UI_DASHBOARD);
        else
            ui_go_home(u);
    } else if (u->device.unlocked) {
        ui_menu_key(u, &home_menu, key);
    } else if (u->device.status == 2) {
        if (key == UI_SELECT) begin_vault_flow(u, FV_ACCESS_FULL);
        else if (key == UI_UP && u->fido_enabled) begin_vault_flow(u, FV_ACCESS_PASSKEYS_ONLY);
        else if (key == UI_DOWN) begin_vault_flow(u, FV_ACCESS_FILES_READ_ONLY);
    } else if (key == UI_SELECT && (u->device.status == 0 || u->device.status == 1 ||
                                    u->device.status == 5)) {
        begin_vault_flow(u, FV_ACCESS_LOCKED);
    }
}

static void lock_panel(fv_ui *u, unsigned x, unsigned y, unsigned width, unsigned height,
                       fv_ui_colour fill, fv_ui_colour edge) {
    for (unsigned row = 0; row < height; ++row) {
        unsigned inset = row == 0 || row == height - 1 ? 1 : 0;
        for (unsigned column = inset; column < width - inset; ++column) {
            bool border = row == 0 || row == height - 1 ||
                          column == inset || column == width - inset - 1;
            ui_draw_pixel(u, x + column, y + row, border ? edge : fill);
        }
    }
}

static void unlock_choice(fv_ui *u, unsigned y, const char *button, const char *label,
                          bool primary) {
    lock_panel(u, 43, y, 114, 15, primary ? 0x09 : 0x05,
               primary ? UI_COLOUR_ACCENT : 0x0a);
    lock_panel(u, 46, y + 3, 13, 9, primary ? UI_COLOUR_ACCENT : 0x0a,
               primary ? UI_COLOUR_ACCENT : 0x0a);
    ui_draw_text_at(u, 50, y + 4, button, 1,
                    primary ? UI_COLOUR_BACKGROUND : UI_COLOUR_TEXT);
    ui_draw_text_at(u, 65, y + 4, label, 1, UI_COLOUR_TEXT);
}

static void render_locked(fv_ui *u) {
    ui_draw_text_at(u, 4, 2, "FUSE VAULT", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 105, 2, "\007 LOCKED", 1, UI_COLOUR_WARNING);
    for (unsigned x = 4; x < 157; ++x)
        ui_draw_pixel(u, x, 12, x < 63 ? UI_COLOUR_ACCENT : 0x05);

    /* A vault and short circuit traces connect the three direct button choices. */
    ui_draw_vault(u, 8, 28, UI_COLOUR_WARNING);
    unsigned top = u->fido_enabled ? 23 : 40;
    for (unsigned y = top; y <= 57; ++y) ui_draw_pixel(u, 38, y, 0x0a);
    for (unsigned x = 33; x <= 38; ++x) ui_draw_pixel(u, x, 40, UI_COLOUR_ACCENT);
    for (unsigned row = u->fido_enabled ? 0 : 1; row < 3; ++row)
        for (unsigned x = 38; x < 43; ++x) ui_draw_pixel(u, x, 23 + row * 17, 0x0a);

    /* Match the physical controls: Up above Select, Down below it. */
    if (u->fido_enabled)
        unlock_choice(u, 16, "\004", "PASSKEYS ONLY", false);
    else
        ui_draw_text_at(u, 65, 20, "UNLOCK VAULT", 1, UI_COLOUR_MUTED);
    unlock_choice(u, 33, "\006", "FULL ACCESS", true);
    unlock_choice(u, 50, "\002", "FILES READ ONLY", false);

    for (unsigned x = 4; x < 157; ++x) ui_draw_pixel(u, x, 67, 0x05);
    ui_draw_text_at(u, 4, 71, "\003\001 FLIP", 1, UI_COLOUR_MUTED);
    ui_draw_text_at(u, 78, 71, "CHOOSE ACCESS", 1, UI_COLOUR_MUTED);
}

static void render_home(fv_ui *u) {
    if (!u->device.unlocked && u->device.status == 2) {
        render_locked(u);
        return;
    }
    char b[40];
    ui_draw_text_at(u, 4, 2, "FUSE VAULT", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 112, 2, u->device.unlocked ? "OPEN" : u->device.status == 2 ? "LOCKED" :
                    (u->device.status == 3 || u->device.status == 4) ? "DENIED" : "SETUP", 1,
                    u->device.unlocked ? UI_COLOUR_SUCCESS : UI_COLOUR_WARNING);
    ui_draw_rule(u, 12);
    if (u->device.unlocked) {
        ui_draw_text_at(u, 4, 17, ui_mode_label(u->device.mode), 1, UI_COLOUR_TEXT);
        /* Arrows point into the vault for writes, out of it for reads. */
        snprintf(b, sizeof(b), "\001\007 %u  \003\007 %u KIB/S",
                 (unsigned)(u->write_kib_tenths / 10), (unsigned)(u->read_kib_tenths / 10));
        ui_draw_text_at(u, 4, 26, fv_access_files(u->device.mode) ? b : "FILES NOT EXPOSED", 1, UI_COLOUR_TEXT);
        ui_draw_rule(u, 36);
        ui_menu_render(u, &home_menu);
    } else {
        ui_draw_vault(u, 68, 17, u->device.status == 2 ? UI_COLOUR_ACCENT : UI_COLOUR_WARNING);
        const char *title = u->device.status == 2                              ? "VAULT LOCKED"
                            : (u->device.status == 3 || u->device.status == 4) ? "ACCESS DISABLED"
                                                                               : "WELCOME";
        ui_draw_text_at(u, (160 - (unsigned)strlen(title) * 6) / 2, 46, title, 1, UI_COLOUR_TEXT);
        const char *action = u->device.status == 2 ? "\006 UNLOCK"
                             : (u->device.status == 3 || u->device.status == 4)
                                 ? "ATTEMPT LIMIT REACHED"
                                 : "\006 SET UP VAULT";
        ui_draw_text_at(u, (160 - (unsigned)strlen(action) * 6) / 2, 57, action, 1, UI_COLOUR_TEXT);
    }
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, u->device.unlocked ? "\005 BACK" : "\003\001 FLIP", 1, UI_COLOUR_TEXT);
    if (u->device.unlocked && fv_access_files(u->device.mode))
        ui_draw_text_at(u, 76, 71, "EJECT FIRST", 1, UI_COLOUR_TEXT);
}

const ui_scene ui_home_scene = {.key = home_key, .render = render_home};
