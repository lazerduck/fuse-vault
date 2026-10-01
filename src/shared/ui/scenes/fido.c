#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static void initialize(fv_ui *u) {
    ui_submit(u, UI_FIDO_INIT);
}
static void cancel_initialize(fv_ui *u) {
    ui_settings_return(u, UI_SETTING_FIDO_INIT);
}

static void initialize_key(fv_ui *u, fv_ui_key key) {
    ui_confirmation_key(u, key, initialize, cancel_initialize);
}
static void policy_enter(fv_ui *u) {
    u->job.fido_policy = u->device.fido_policy;
}
static void policy_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK)
        ui_settings_return(u, UI_SETTING_FIDO_POLICY);
    else if (key == UI_SELECT)
        ui_submit(u, UI_FIDO_POLICY);
    else
        u->job.fido_policy ^= 1;
}
static void approve_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_SELECT || key == UI_BACK) {
        u->fido_done = true;
        u->fido_approved = key == UI_SELECT;
    }
}

static void render_fido_init_confirm(fv_ui *u) {
    ui_draw_header(u, "RESET FIDO?", NULL);
    ui_draw_text_at(u, 4, 16, "ERASES ALL PASSKEYS", 1, UI_COLOUR_WARNING);
    ui_draw_text_at(u, 4, 26, "KEEPS USB FILES", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 4, 36, "RESETS FIDO SETTINGS", 1, UI_COLOUR_TEXT);
    ui_draw_danger_choices(u, "RESET");
}

static void render_fido_policy_screen(fv_ui *u) {
    ui_draw_header(u, "FIDO VERIFICATION", NULL);
    ui_draw_choice(u, 16, "TIMED / SAME SITE", !u->job.fido_policy);
    ui_draw_choice(u, 28, "WHILE UNLOCKED", u->job.fido_policy != 0);
    ui_draw_text_at(
        u, 4, 43, u->job.fido_policy ? "NO TIME OR SITE LIMIT" : "FIRST 30S / MAX 10 MIN", 1, UI_COLOUR_TEXT);
    ui_draw_text_at(u, 4, 55, "ALWAYS ASK FOR APPROVAL", 1, UI_COLOUR_TEXT);
    ui_draw_footer(u, "\005 BACK", "\006 SAVE");
}

static void render_fido_approve(fv_ui *u) {
    const char *site = u->fido_label;
    const char *title = "PASSKEY REQUEST";
    if (!strncmp(site, "SIGN IN ", 8)) { title = "APPROVE SIGN IN?"; site += 8; }
    else if (!strncmp(site, "REGISTER ", 9)) { title = "CREATE PASSKEY?"; site += 9; }
    ui_draw_header(u, title, NULL);
    size_t length = strlen(site);
    /* Short requests get a quiet instruction; long sites keep all five rows. */
    unsigned top = length <= 78 ? 30 : 16;
    if (length <= 78) ui_draw_text_at(u, 4, 16, "CHECK WEBSITE", 1, UI_COLOUR_MUTED);
    for (unsigned i = 0; i < 5 && i*26 < length; i++) {
        char part[27] = {0};
        strncpy(part, site + i*26, 26);
        ui_draw_text_at(u, 4, top + i*10, part, 1, UI_COLOUR_TEXT);
    }
    ui_draw_footer(u, "\005 REJECT", "\006 APPROVE");
}

const ui_scene ui_fido_init_scene = {.key = initialize_key, .render = render_fido_init_confirm};
const ui_scene ui_fido_policy_scene = {
    .enter = policy_enter, .key = policy_key, .render = render_fido_policy_screen};
const ui_scene ui_fido_approve_scene = {.key = approve_key, .render = render_fido_approve};
