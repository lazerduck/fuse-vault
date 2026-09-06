#ifndef FUSE_VAULT_SETTINGS_H
#define FUSE_VAULT_SETTINGS_H

#include "fuse_vault/app.h"

/* Local settings sub-flow: navigation/drafts only, no persistence or USB calls.
 * app.c owns authentication, locking, faults and entry/exit authorization. */
typedef enum {
    FV_SETTING_PASSWORD,
    FV_SETTING_ENTRY_METHOD,
} fv_setting_id_t;

typedef struct {
    fv_setting_id_t id;
    const char *label;
} fv_setting_item_t;

size_t fv_settings_count(void);
const fv_setting_item_t *fv_settings_item(size_t index);
bool fv_settings_owns_state(fv_state_t state);
void fv_settings_clear(fv_app_t *app);
fv_command_set_t fv_settings_handle(fv_app_t *app, fv_event_t event);
void fv_settings_render(const fv_app_t *app, fv_ui_view_t *view);

#endif
