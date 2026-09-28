#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include "settings.h"

static void erase(fv_ui *u) {
    ui_submit(u, UI_ERASE);
}

static void cancel_erase(fv_ui *u) {
    ui_settings_return(u, UI_SETTING_ERASE);
}

static void erase_key(fv_ui *u, fv_ui_key key) {
    ui_confirmation_key(u, key, erase, cancel_erase);
}

static void render_erase_confirm(fv_ui *u) {
    ui_draw_header(u, "DESTROY VAULT?", NULL);
    ui_draw_text_at(u, 4, 16, "LOSES FILES AND PASSKEYS", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 4, 26, "UNMOUNT DRIVE FIRST", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 4, 36, "USES A SETUP SLOT", 1, UI_COLOUR_TEXT);
    ui_draw_confirm_choices(u, "DESTROY");
}

const ui_scene ui_erase_scene = {.key = erase_key, .render = render_erase_confirm};
