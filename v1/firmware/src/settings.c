#include "fuse_vault/settings.h"
#include "fuse_vault/secret_input.h"

/* Stable action IDs are independent of menu ordering. Add future settings here
 * and give their workflows explicit states; never execute effects in a renderer. */
static const fv_setting_item_t ITEMS[] = {
    {FV_SETTING_PASSWORD, "Change password"},
    {FV_SETTING_ENTRY_METHOD, "Change entry method"},
};

size_t fv_settings_count(void) { return sizeof(ITEMS) / sizeof(ITEMS[0]); }
const fv_setting_item_t *fv_settings_item(size_t index) {
    return index < fv_settings_count() ? &ITEMS[index] : NULL;
}

bool fv_settings_owns_state(fv_state_t state) {
    switch (state) {
        case FV_STATE_SETTINGS:
        case FV_STATE_CHANGE_METHOD:
        case FV_STATE_CHANGE_SECRET:
        case FV_STATE_CHANGE_CONFIRM:
        case FV_STATE_CHANGE_MISMATCH:
        case FV_STATE_CHANGE_REVIEW:
        case FV_STATE_CHANGE_SAVING:
        case FV_STATE_CHANGE_SAVED:
            return true;
        default: return false;
    }
}

void fv_settings_clear(fv_app_t *app) {
    fv_secret_entry_clear(&app->change_secret_entry);
    app->change_entry_method = app->selected_entry_method;
}

static void begin_entry(fv_app_t *app) {
    fv_secret_entry_clear(&app->change_secret_entry);
    fv_secret_entry_begin(&app->secret_entry, app->change_entry_method);
    app->state = FV_STATE_CHANGE_SECRET;
}

static void cancel(fv_app_t *app) {
    fv_secret_entry_clear(&app->secret_entry);
    fv_settings_clear(app);
    app->state = FV_STATE_SETTINGS;
}

static bool entries_match(const fv_secret_entry_t *a, const fv_secret_entry_t *b) {
    fv_secret_encoding_t left = {0}, right = {0};
    bool valid = fv_secret_entry_encode(a, &left) && fv_secret_entry_encode(b, &right);
    uint8_t difference = 0u;
    for (size_t i = 0; i < sizeof(left.bytes); ++i)
        difference |= left.bytes[i] ^ right.bytes[i];
    volatile uint8_t *l = left.bytes, *r = right.bytes;
    for (size_t i = 0; i < sizeof(left.bytes); ++i) { l[i] = 0u; r[i] = 0u; }
    return valid && difference == 0u;
}

fv_command_set_t fv_settings_handle(fv_app_t *app, fv_event_t event) {
    switch (app->state) {
        case FV_STATE_SETTINGS:
            if (event == FV_EVENT_UP || event == FV_EVENT_DOWN) {
                size_t count = fv_settings_count();
                app->selected_setting = (uint8_t)((app->selected_setting +
                    (event == FV_EVENT_UP ? count - 1u : 1u)) % count);
            } else if (event == FV_EVENT_SELECT) {
                const fv_setting_item_t *item = fv_settings_item(app->selected_setting);
                if (!item) break;
                fv_settings_clear(app);
                switch (item->id) {
                    case FV_SETTING_PASSWORD: begin_entry(app); break;
                    case FV_SETTING_ENTRY_METHOD: app->state = FV_STATE_CHANGE_METHOD; break;
                }
            }
            break;
        case FV_STATE_CHANGE_METHOD:
            if (event == FV_EVENT_UP || event == FV_EVENT_DOWN)
                app->change_entry_method = (fv_entry_method_t)((app->change_entry_method +
                    (event == FV_EVENT_UP ? FV_ENTRY_METHOD_COUNT - 1 : 1)) % FV_ENTRY_METHOD_COUNT);
            else if (event == FV_EVENT_SELECT) begin_entry(app);
            else if (event == FV_EVENT_BACK) cancel(app);
            break;
        case FV_STATE_CHANGE_SECRET:
        case FV_STATE_CHANGE_CONFIRM: {
            fv_secret_event_result_t result = fv_secret_entry_handle(&app->secret_entry, event);
            if (result == FV_SECRET_EVENT_COMPLETE) {
                if (app->state == FV_STATE_CHANGE_SECRET) {
                    app->change_secret_entry = app->secret_entry;
                    fv_secret_entry_begin(&app->secret_entry, app->change_entry_method);
                    app->state = FV_STATE_CHANGE_CONFIRM;
                } else {
                    bool matches = entries_match(&app->secret_entry, &app->change_secret_entry);
                    fv_secret_entry_clear(&app->secret_entry);
                    app->state = matches ? FV_STATE_CHANGE_REVIEW : FV_STATE_CHANGE_MISMATCH;
                    if (!matches) fv_secret_entry_clear(&app->change_secret_entry);
                }
            } else if (event == FV_EVENT_BACK && result == FV_SECRET_EVENT_IGNORED)
                cancel(app);
            break;
        }
        case FV_STATE_CHANGE_MISMATCH:
            if (event == FV_EVENT_SELECT) begin_entry(app);
            else if (event == FV_EVENT_BACK) cancel(app);
            break;
        case FV_STATE_CHANGE_REVIEW:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_CHANGE_SAVING;
                return FV_COMMAND_CHANGE_CREDENTIAL;
            }
            if (event == FV_EVENT_BACK) cancel(app);
            break;
        case FV_STATE_CHANGE_SAVING:
            if (event == FV_EVENT_CREDENTIAL_CHANGED) {
                app->selected_entry_method = app->change_entry_method;
                cancel(app);
                app->state = FV_STATE_CHANGE_SAVED;
                app->session_unlocked = false;
                app->selected_mode = FV_MODE_VAULT;
                return FV_COMMAND_USB_DETACH | FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            break;
        default: break;
    }
    return FV_COMMAND_NONE;
}
