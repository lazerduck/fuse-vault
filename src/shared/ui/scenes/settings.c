#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include <stdio.h>

/* Stable item IDs also identify the selection restored by child scenes. */
#include "settings.h"

static void begin_policy(fv_ui *u) {
    ui_clear_input(u);
    u->flow = UI_FLOW_POLICY;
    u->job.attempts = u->device.attempts;
    u->job.action = u->device.action;
    u->job.profile = u->device.profile ? u->device.profile : 2;
    ui_scene_show(u, UI_SECRET);
}

static void begin_change(fv_ui *u) {
    ui_clear_input(u);
    u->flow = UI_FLOW_CHANGE;
    u->job.profile = u->device.profile ? u->device.profile : 2;
    u->job.attempts = u->device.attempts;
    u->job.action = u->device.action;
    ui_scene_show(u, UI_SECRET);
}

static void begin_erase(fv_ui *u) {
    ui_scene_show(u, UI_ERASE_CONFIRM);
}
static void begin_fido_init(fv_ui *u) {
    ui_scene_show(u, UI_FIDO_INIT_CONFIRM);
}
static void begin_fido_policy(fv_ui *u) {
    ui_scene_show(u, UI_FIDO_POLICY_SCREEN);
}
static void begin_passkeys(fv_ui *u) {
    u->job.passkey_index = 0;
    ui_submit(u, UI_PASSKEY_LIST);
}

static const ui_menu_item settings_items[] = {
    [UI_SETTING_POLICY] = {"FAILURE POLICY", begin_policy},
    [UI_SETTING_ERASE] = {"ERASE AND SET UP", begin_erase},
    [UI_SETTING_METHOD] = {"UNLOCK METHOD", begin_change},
    [UI_SETTING_FIDO_INIT] = {"INITIALIZE FIDO", begin_fido_init},
    [UI_SETTING_FIDO_POLICY] = {"FIDO VERIFICATION", begin_fido_policy},
    [UI_SETTING_PASSKEYS] = {"PASSKEYS", begin_passkeys},
};

static ui_menu settings_menu(const fv_ui *u) {
    return (ui_menu){settings_items,
                     u->fido_enabled ? UI_SETTING_COUNT : UI_SETTING_FIDO_INIT,
                     15,
                     12,
                     11,
                     2,
                     4};
}

void ui_settings_return(fv_ui *u, unsigned item) {
    ui_clear_input(u);
    ui_scene_show(u, UI_SETTINGS);
    u->cursor = item;
}

static void settings_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK)
        ui_go_home(u);
    else {
        ui_menu menu = settings_menu(u);
        ui_menu_key(u, &menu, key);
    }
}

static void render_settings(fv_ui *u) {
    ui_menu menu = settings_menu(u);
    char position[16];
    ui_draw_text_at(u, 4, 2, "SETTINGS", 1, UI_COLOUR_TEXT);
    snprintf(position, sizeof(position), "%u/%u", u->cursor + 1, menu.count);
    ui_draw_text_at(u, 130, 2, position, 1, UI_COLOUR_TEXT);
    ui_draw_rule(u, 12);
    ui_menu_render(u, &menu);
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, "\005 BACK", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 76, 71, "EJECT FIRST", 1, UI_COLOUR_TEXT);
}

const ui_scene ui_settings_scene = {.key = settings_key, .render = render_settings};
