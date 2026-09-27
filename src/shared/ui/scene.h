#ifndef FV_UI_SCENE_H
#define FV_UI_SCENE_H
#include "device_ui.h"

/* Private scene interface. The manager routes logical keys to exactly one scene.
 * Scene handlers update state and request transitions; the core renders afterwards.
 * Enter runs on every transition, including re-entry. No heap or subscriptions. */
typedef struct {
    void (*enter)(fv_ui *);
    void (*key)(fv_ui *, fv_ui_key);
    void (*render)(fv_ui *);
} ui_scene;

const ui_scene *ui_scene_get(fv_ui_screen screen);
void ui_scene_show(fv_ui *, fv_ui_screen);
void ui_submit(fv_ui *, fv_ui_operation);
void ui_clear_input(fv_ui *);
void ui_go_home(fv_ui *);
void ui_settings_return(fv_ui *, unsigned item);

extern const ui_scene ui_home_scene, ui_settings_scene;
extern const ui_scene ui_method_scene, ui_credential_scene;
extern const ui_scene ui_stack_scene, ui_options_scene, ui_review_scene;
extern const ui_scene ui_erase_scene, ui_fido_init_scene, ui_fido_policy_scene;
extern const ui_scene ui_fido_approve_scene, ui_passkeys_scene, ui_passkey_delete_scene;
extern const ui_scene ui_wait_scene, ui_error_scene;
#endif
