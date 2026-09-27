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

static void begin_vault_flow(fv_ui *u) {
    memset(&u->job, 0, sizeof(u->job));
    u->error = 0;
    u->job.count = 1;
    u->job.algorithms[0] = 1;
    u->job.attempts = 10;
    u->job.action = 1;
    u->job.profile = u->device.profile ? u->device.profile : 2;
    if (u->device.status == 2) {
        u->flow = UI_FLOW_UNLOCK;
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
            lock_vault(u);
        else
            ui_go_home(u);
    } else if (u->device.unlocked) {
        ui_menu_key(u, &home_menu, key);
    } else if (key == UI_SELECT && (u->device.status == 0 || u->device.status == 1 ||
                                    u->device.status == 2 || u->device.status == 5)) {
        begin_vault_flow(u);
    }
}

static void render_home(fv_ui *u) {
    char b[40];
    ui_draw_text_at(u, 4, 2, "FUSE VAULT", 1, true);
    ui_draw_text_at(u, 112, 2, u->device.unlocked ? "OPEN" : "LOCKED", 1, true);
    ui_draw_rule(u, 12);
    if (u->device.unlocked) {
        uint64_t tenths = u->device.blocks * 10 / 2097152;
        snprintf(b, sizeof(b), "%llu.%llu GIB VAULT", (unsigned long long)(tenths / 10),
                 (unsigned long long)(tenths % 10));
        ui_draw_text_at(u, 4, 17, b, 1, true);
        /* Arrows point into the vault for writes, out of it for reads. */
        snprintf(b, sizeof(b), "\001\007 %u  \003\007 %u KIB/S",
                 (unsigned)(u->write_kib_tenths / 10), (unsigned)(u->read_kib_tenths / 10));
        ui_draw_text_at(u, 4, 26, b, 1, true);
        ui_draw_rule(u, 36);
        ui_menu_render(u, &home_menu);
    } else {
        const char *title = u->device.status == 2                              ? "VAULT LOCKED"
                            : (u->device.status == 3 || u->device.status == 4) ? "ACCESS DISABLED"
                                                                               : "WELCOME";
        ui_draw_text_at(u, (160 - (unsigned)strlen(title) * 6) / 2, 29, title, 1, true);
        const char *action = u->device.status == 2 ? "\006 UNLOCK"
                             : (u->device.status == 3 || u->device.status == 4)
                                 ? "ATTEMPT LIMIT REACHED"
                                 : "\006 SET UP VAULT";
        ui_draw_text_at(u, (160 - (unsigned)strlen(action) * 6) / 2, 48, action, 1, true);
    }
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, u->device.unlocked ? "\005 LOCK" : "\003\001 FLIP", 1, true);
    if (u->device.unlocked)
        ui_draw_text_at(u, 76, 71, "EJECT FIRST", 1, true);
}

const ui_scene ui_home_scene = {.key = home_key, .render = render_home};
