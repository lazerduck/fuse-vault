#include "scene.h"

/* This is the only screen registry. Related screens can share a scene handler. */
static const ui_scene *const scenes[UI_SCREEN_COUNT] = {
    [UI_WAIT] = &ui_wait_scene,
    [UI_HOME] = &ui_home_scene,
    [UI_DASHBOARD] = &ui_dashboard_scene,
    [UI_DASHBOARD_SETTING] = &ui_dashboard_setting_scene,
    [UI_SECRET] = &ui_credential_scene,
    [UI_CONFIRM] = &ui_credential_scene,
    [UI_STACK] = &ui_stack_scene,
    [UI_OPTIONS] = &ui_options_scene,
    [UI_REVIEW] = &ui_review_scene,
    [UI_ERASE_CONFIRM] = &ui_erase_scene,
    [UI_ERROR] = &ui_error_scene,
    [UI_METHOD] = &ui_method_scene,
    [UI_SETTINGS] = &ui_settings_scene,
    [UI_FIDO_APPROVE] = &ui_fido_approve_scene,
    [UI_FIDO_INIT_CONFIRM] = &ui_fido_init_scene,
    [UI_FIDO_POLICY_SCREEN] = &ui_fido_policy_scene,
    [UI_PASSKEYS] = &ui_passkeys_scene,
    [UI_PASSKEY_DELETE_CONFIRM] = &ui_passkey_delete_scene,
};

const ui_scene *ui_scene_get(fv_ui_screen screen) {
    if ((unsigned)screen >= UI_SCREEN_COUNT)
        return NULL;
    return scenes[screen];
}

void ui_scene_show(fv_ui *u, fv_ui_screen screen) {
    const ui_scene *scene = ui_scene_get(screen);
    if (!scene)
        return;
    u->screen = screen;
    u->cursor = 0;
    if (scene->enter)
        scene->enter(u);
}
