#include "fuse_vault/input.h"

#include <stddef.h>

static void bind(fv_input_map_t *map, fv_input_id_t input, fv_event_t event,
                 bool repeat) {
    (void)fv_input_map_bind(map, input, event, repeat);
}

static void bind_directions(fv_input_map_t *map, bool repeat) {
    bind(map, FV_INPUT_UP, FV_EVENT_UP, repeat);
    bind(map, FV_INPUT_DOWN, FV_EVENT_DOWN, repeat);
    bind(map, FV_INPUT_LEFT, FV_EVENT_LEFT, repeat);
    bind(map, FV_INPUT_RIGHT, FV_EVENT_RIGHT, repeat);
}

void fv_input_map_for_app(const fv_app_t *app, fv_input_map_t *map) {
    if (map == NULL) return;
    fv_input_map_clear(map);
    if (app == NULL) return;
    switch (app->state) {
        case FV_STATE_SETUP_REQUIRED:
            bind(map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
            break;
        case FV_STATE_SETUP_METHOD_SELECT:
            bind(map, FV_INPUT_UP, FV_EVENT_UP, true);
            bind(map, FV_INPUT_DOWN, FV_EVENT_DOWN, true);
            bind(map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
            bind(map, FV_INPUT_BACK, FV_EVENT_BACK, false);
            break;
        case FV_STATE_SETUP_SECRET_ENTRY:
        case FV_STATE_SETUP_SECRET_CONFIRM:
        case FV_STATE_VAULT_SECRET_ENTRY: {
            /* Repeating a direction would silently change a direction secret. */
            const bool repeat = app->selected_entry_method !=
                                FV_ENTRY_METHOD_DIRECTIONS;
            bind_directions(map, repeat);
            bind(map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
            bind(map, FV_INPUT_BACK, FV_EVENT_BACK, repeat);
            break;
        }
        case FV_STATE_SETUP_SECRET_MISMATCH:
        case FV_STATE_SETUP_POLICY_CONFIRM:
            bind(map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
            bind(map, FV_INPUT_BACK, FV_EVENT_BACK, false);
            break;
        case FV_STATE_MODE_SELECT:
            bind(map, FV_INPUT_UP, FV_EVENT_UP, true);
            bind(map, FV_INPUT_DOWN, FV_EVENT_DOWN, true);
            bind(map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
            break;
        case FV_STATE_VAULT_UNLOCKED:
        case FV_STATE_FIDO_READY:
            bind(map, FV_INPUT_BACK, FV_EVENT_BACK, false);
            break;
        case FV_STATE_BOOTING:
        case FV_STATE_PROVISIONING:
        case FV_STATE_VAULT_RESERVING_ATTEMPT:
        case FV_STATE_VAULT_AUTHENTICATING:
        case FV_STATE_VAULT_RECORDING_SUCCESS:
        case FV_STATE_DESTROYED:
        case FV_STATE_FAULT:
            break;
    }
}
