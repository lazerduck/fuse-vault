#include "fuse_vault/app.h"

#include <stdio.h>
#include <string.h>

static void clear_wheels(uint8_t wheels[FV_SECRET_WHEEL_COUNT]) {
    memset(wheels, 0, FV_SECRET_WHEEL_COUNT * sizeof(wheels[0]));
}

static fv_command_set_t enter_fault(fv_app_t *app) {
    clear_wheels(app->secret_wheels);
    clear_wheels(app->setup_secret_wheels);
    app->state = FV_STATE_FAULT;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

static void begin_wheel_entry(fv_app_t *app) {
    clear_wheels(app->secret_wheels);
    app->selected_secret_wheel = 0u;
}

static bool handle_wheel_event(fv_app_t *app, fv_event_t event) {
    if (event == FV_EVENT_LEFT) {
        app->selected_secret_wheel = (uint8_t)(
            app->selected_secret_wheel == 0u
                ? FV_SECRET_WHEEL_COUNT - 1u
                : (unsigned)app->selected_secret_wheel - 1u);
    } else if (event == FV_EVENT_RIGHT) {
        app->selected_secret_wheel =
            (uint8_t)((app->selected_secret_wheel + 1u) %
                      FV_SECRET_WHEEL_COUNT);
    } else if (event == FV_EVENT_UP) {
        uint8_t *value = &app->secret_wheels[app->selected_secret_wheel];
        *value = (uint8_t)((*value + 1u) % FV_SECRET_WHEEL_VALUES);
    } else if (event == FV_EVENT_DOWN) {
        uint8_t *value = &app->secret_wheels[app->selected_secret_wheel];
        *value = *value == 0u ? FV_SECRET_WHEEL_VALUES - 1u
                              : (uint8_t)(*value - 1u);
    } else {
        return false;
    }
    return true;
}

static fv_command_set_t leave_sensitive_mode(fv_app_t *app) {
    clear_wheels(app->secret_wheels);
    app->selected_secret_wheel = 0u;
    app->state = FV_STATE_MODE_SELECT;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

void fv_app_init(fv_app_t *app, bool provisioned, uint8_t persisted_failed_attempts) {
    if (app == NULL) {
        return;
    }

    *app = (fv_app_t) {
        .state = FV_STATE_BOOTING,
        .selected_mode = FV_MODE_VAULT,
        .selected_entry_method = FV_ENTRY_METHOD_WHEELS,
        .failed_attempts = persisted_failed_attempts > FV_MAX_UNLOCK_ATTEMPTS
            ? FV_MAX_UNLOCK_ATTEMPTS
            : persisted_failed_attempts,
        .provisioned = provisioned,
    };
}

fv_command_set_t fv_app_handle(fv_app_t *app, fv_event_t event) {
    if (app == NULL) {
        return FV_COMMAND_NONE;
    }

    if (event == FV_EVENT_FATAL_ERROR || event == FV_EVENT_STORAGE_FAILED) {
        return enter_fault(app);
    }

    switch (app->state) {
        case FV_STATE_BOOTING:
            if (event == FV_EVENT_BOOT_COMPLETED) {
                if (app->provisioned && app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
                    app->state = FV_STATE_DESTROYED;
                    return FV_COMMAND_USB_DETACH |
                           FV_COMMAND_ERASE_TRANSIENT_SECRET |
                           FV_COMMAND_ERASE_SESSION_KEYS |
                           FV_COMMAND_DESTROY_DEVICE_SECRET;
                }
                app->state = app->provisioned ? FV_STATE_MODE_SELECT
                                              : FV_STATE_SETUP_REQUIRED;
            }
            break;

        case FV_STATE_SETUP_REQUIRED:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_SETUP_METHOD_SELECT;
            }
            break;

        case FV_STATE_SETUP_METHOD_SELECT:
            if (event == FV_EVENT_SELECT) {
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            } else if (event == FV_EVENT_BACK) {
                app->state = FV_STATE_SETUP_REQUIRED;
            }
            break;

        case FV_STATE_SETUP_SECRET_ENTRY:
            if (handle_wheel_event(app, event)) {
                break;
            }
            if (event == FV_EVENT_SELECT) {
                memcpy(app->setup_secret_wheels, app->secret_wheels,
                       sizeof(app->setup_secret_wheels));
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_SECRET_CONFIRM;
            } else if (event == FV_EVENT_BACK) {
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_METHOD_SELECT;
            }
            break;

        case FV_STATE_SETUP_SECRET_CONFIRM:
            if (handle_wheel_event(app, event)) {
                break;
            }
            if (event == FV_EVENT_SELECT) {
                if (memcmp(app->secret_wheels, app->setup_secret_wheels,
                           sizeof(app->secret_wheels)) == 0) {
                    clear_wheels(app->secret_wheels);
                    app->selected_secret_wheel = 0u;
                    app->state = FV_STATE_SETUP_POLICY_CONFIRM;
                } else {
                    clear_wheels(app->secret_wheels);
                    clear_wheels(app->setup_secret_wheels);
                    app->selected_secret_wheel = 0u;
                    app->state = FV_STATE_SETUP_SECRET_MISMATCH;
                }
            } else if (event == FV_EVENT_BACK) {
                clear_wheels(app->setup_secret_wheels);
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_SETUP_SECRET_MISMATCH:
            if (event == FV_EVENT_SELECT || event == FV_EVENT_BACK) {
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_SETUP_POLICY_CONFIRM:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_PROVISIONING;
                return FV_COMMAND_BEGIN_PROVISIONING;
            }
            if (event == FV_EVENT_BACK) {
                clear_wheels(app->setup_secret_wheels);
                begin_wheel_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_PROVISIONING:
            if (event == FV_EVENT_PROVISIONING_SUCCEEDED) {
                clear_wheels(app->setup_secret_wheels);
                app->provisioned = true;
                app->failed_attempts = 0u;
                app->state = FV_STATE_MODE_SELECT;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            if (event == FV_EVENT_PROVISIONING_FAILED) {
                clear_wheels(app->setup_secret_wheels);
                app->state = FV_STATE_SETUP_REQUIRED;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            break;

        case FV_STATE_MODE_SELECT:
            if (event == FV_EVENT_UP || event == FV_EVENT_DOWN) {
                app->selected_mode = app->selected_mode == FV_MODE_VAULT
                    ? FV_MODE_FIDO
                    : FV_MODE_VAULT;
            } else if (event == FV_EVENT_SELECT) {
                if (app->selected_mode == FV_MODE_VAULT) {
                    begin_wheel_entry(app);
                    app->state = FV_STATE_VAULT_SECRET_ENTRY;
                } else {
                    app->state = FV_STATE_FIDO_READY;
                    return FV_COMMAND_USB_ATTACH_FIDO;
                }
            }
            break;

        case FV_STATE_VAULT_SECRET_ENTRY:
            if (handle_wheel_event(app, event)) {
                break;
            }
            if (event == FV_EVENT_SELECT) {
                if (app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
                    clear_wheels(app->secret_wheels);
                    app->state = FV_STATE_DESTROYED;
                    return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                           FV_COMMAND_DESTROY_DEVICE_SECRET;
                }
                ++app->failed_attempts;
                app->state = FV_STATE_VAULT_RESERVING_ATTEMPT;
                return FV_COMMAND_STORE_ATTEMPT_COUNTER;
            } else if (event == FV_EVENT_BACK) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_VAULT_RESERVING_ATTEMPT:
            if (event == FV_EVENT_ATTEMPT_COUNTER_STORED) {
                app->state = FV_STATE_VAULT_AUTHENTICATING;
                return FV_COMMAND_BEGIN_AUTHENTICATION;
            }
            break;

        case FV_STATE_VAULT_AUTHENTICATING:
            if (event == FV_EVENT_AUTH_SUCCEEDED) {
                clear_wheels(app->secret_wheels);
                app->failed_attempts = 0u;
                app->state = FV_STATE_VAULT_RECORDING_SUCCESS;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_STORE_ATTEMPT_COUNTER;
            }
            if (event == FV_EVENT_AUTH_FAILED) {
                clear_wheels(app->secret_wheels);
                if (app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
                    app->state = FV_STATE_DESTROYED;
                    return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                           FV_COMMAND_ERASE_SESSION_KEYS |
                           FV_COMMAND_USB_DETACH |
                           FV_COMMAND_DESTROY_DEVICE_SECRET;
                }
                app->state = FV_STATE_VAULT_SECRET_ENTRY;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            break;

        case FV_STATE_VAULT_RECORDING_SUCCESS:
            if (event == FV_EVENT_ATTEMPT_COUNTER_STORED) {
                app->state = FV_STATE_VAULT_UNLOCKED;
                return FV_COMMAND_USB_ATTACH_MSC;
            }
            break;

        case FV_STATE_VAULT_UNLOCKED:
            if (event == FV_EVENT_LOCK_REQUESTED ||
                event == FV_EVENT_USB_EJECTED ||
                event == FV_EVENT_BACK) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_FIDO_READY:
            if (event == FV_EVENT_BACK || event == FV_EVENT_LOCK_REQUESTED) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_DESTROYED:
        case FV_STATE_FAULT:
            break;
    }

    return FV_COMMAND_NONE;
}

static void clear_view(fv_ui_view_t *view) {
    memset(view, 0, sizeof(*view));
}

static void render_wheels(const fv_app_t *app,
                          char line[FV_UI_TEXT_CAPACITY]) {
    snprintf(line, FV_UI_TEXT_CAPACITY,
             app->selected_secret_wheel == 0u
                 ? "[%02u]  %02u   %02u"
                 : app->selected_secret_wheel == 1u
                     ? " %02u  [%02u]  %02u"
                     : " %02u   %02u  [%02u]",
             (unsigned)app->secret_wheels[0],
             (unsigned)app->secret_wheels[1],
             (unsigned)app->secret_wheels[2]);
}

void fv_app_render(const fv_app_t *app, fv_ui_view_t *view) {
    if (app == NULL || view == NULL) {
        return;
    }

    clear_view(view);

    switch (app->state) {
        case FV_STATE_BOOTING:
            snprintf(view->title, sizeof(view->title), "Fuse Vault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Starting...");
            break;
        case FV_STATE_SETUP_REQUIRED:
            snprintf(view->title, sizeof(view->title), "Setup required");
            snprintf(view->lines[0], sizeof(view->lines[0]), "No vault configured");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Select: begin setup");
            break;
        case FV_STATE_SETUP_METHOD_SELECT:
            snprintf(view->title, sizeof(view->title), "Entry method");
            snprintf(view->lines[0], sizeof(view->lines[0]), "> Number wheels");
            snprintf(view->lines[2], sizeof(view->lines[2]), "More methods later");
            snprintf(view->lines[3], sizeof(view->lines[3]), "OK: choose  Back: cancel");
            break;
        case FV_STATE_SETUP_SECRET_ENTRY:
            snprintf(view->title, sizeof(view->title), "Create secret");
            render_wheels(app, view->lines[0]);
            snprintf(view->lines[2], sizeof(view->lines[2]), "Remember this combination");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Arrows: adjust  OK: next");
            break;
        case FV_STATE_SETUP_SECRET_CONFIRM:
            snprintf(view->title, sizeof(view->title), "Confirm secret");
            render_wheels(app, view->lines[0]);
            snprintf(view->lines[2], sizeof(view->lines[2]), "Enter it again");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Arrows: adjust  OK: check");
            break;
        case FV_STATE_SETUP_SECRET_MISMATCH:
            snprintf(view->title, sizeof(view->title), "Secrets differ");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Nothing was saved");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Enter a new secret");
            snprintf(view->lines[3], sizeof(view->lines[3]), "OK: try again");
            break;
        case FV_STATE_SETUP_POLICY_CONFIRM:
            snprintf(view->title, sizeof(view->title), "No recovery");
            snprintf(view->lines[0], sizeof(view->lines[0]), "10 failures destroy secret");
            snprintf(view->lines[1], sizeof(view->lines[1]), "and encrypted data");
            snprintf(view->lines[3], sizeof(view->lines[3]), "OK: accept  Back: change");
            break;
        case FV_STATE_PROVISIONING:
            snprintf(view->title, sizeof(view->title), "Provisioning");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Creating encrypted vault");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Do not remove power");
            break;
        case FV_STATE_MODE_SELECT:
            snprintf(view->title, sizeof(view->title), "Select mode");
            snprintf(view->lines[0], sizeof(view->lines[0]), "%s Vault storage",
                     app->selected_mode == FV_MODE_VAULT ? ">" : " ");
            snprintf(view->lines[1], sizeof(view->lines[1]), "%s FIDO2 key",
                     app->selected_mode == FV_MODE_FIDO ? ">" : " ");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Up/down + select");
            break;
        case FV_STATE_VAULT_SECRET_ENTRY:
            snprintf(view->title, sizeof(view->title), "Unlock vault");
            render_wheels(app, view->lines[0]);
            snprintf(view->lines[2], sizeof(view->lines[2]), "Failures: %u/%u",
                     (unsigned)app->failed_attempts,
                     (unsigned)FV_MAX_UNLOCK_ATTEMPTS);
            snprintf(view->lines[3], sizeof(view->lines[3]),
                     "Arrows: adjust  OK: submit");
            break;
        case FV_STATE_VAULT_RESERVING_ATTEMPT:
            snprintf(view->title, sizeof(view->title), "Unlock vault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Reserving attempt...");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Do not remove power");
            break;
        case FV_STATE_VAULT_AUTHENTICATING:
            snprintf(view->title, sizeof(view->title), "Unlock vault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Authenticating...");
            break;
        case FV_STATE_VAULT_RECORDING_SUCCESS:
            snprintf(view->title, sizeof(view->title), "Unlock vault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Saving security state");
            break;
        case FV_STATE_VAULT_UNLOCKED:
            snprintf(view->title, sizeof(view->title), "Vault unlocked");
            snprintf(view->lines[0], sizeof(view->lines[0]), "USB storage active");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Eject before removal");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Back: lock");
            break;
        case FV_STATE_FIDO_READY:
            snprintf(view->title, sizeof(view->title), "FIDO2 mode");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Authenticator active");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Back: disconnect");
            break;
        case FV_STATE_DESTROYED:
            snprintf(view->title, sizeof(view->title), "Vault destroyed");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Attempt limit reached");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Data is unrecoverable");
            break;
        case FV_STATE_FAULT:
            snprintf(view->title, sizeof(view->title), "Device fault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "USB disconnected");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Restart required");
            break;
    }
}

const char *fv_state_name(fv_state_t state) {
    static const char *const names[] = {
        "booting",
        "setup-required",
        "setup-method-select",
        "setup-secret-entry",
        "setup-secret-confirm",
        "setup-secret-mismatch",
        "setup-policy-confirm",
        "provisioning",
        "mode-select",
        "vault-secret-entry",
        "vault-reserving-attempt",
        "vault-authenticating",
        "vault-recording-success",
        "vault-unlocked",
        "fido-ready",
        "destroyed",
        "fault",
    };

    if ((unsigned)state >= (sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[state];
}
