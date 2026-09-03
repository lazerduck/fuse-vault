#include "fuse_vault/app.h"

#include <stdio.h>
#include <string.h>

static fv_command_set_t enter_fault(fv_app_t *app) {
    app->state = FV_STATE_FAULT;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

static fv_command_set_t leave_sensitive_mode(fv_app_t *app) {
    memset(app->secret_wheels, 0, sizeof(app->secret_wheels));
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
                app->state = FV_STATE_PROVISIONING;
                return FV_COMMAND_BEGIN_PROVISIONING;
            }
            break;

        case FV_STATE_PROVISIONING:
            if (event == FV_EVENT_PROVISIONING_SUCCEEDED) {
                app->provisioned = true;
                app->failed_attempts = 0u;
                app->state = FV_STATE_MODE_SELECT;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            if (event == FV_EVENT_PROVISIONING_FAILED) {
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
                    memset(app->secret_wheels, 0, sizeof(app->secret_wheels));
                    app->selected_secret_wheel = 0u;
                    app->state = FV_STATE_VAULT_SECRET_ENTRY;
                } else {
                    app->state = FV_STATE_FIDO_READY;
                    return FV_COMMAND_USB_ATTACH_FIDO;
                }
            }
            break;

        case FV_STATE_VAULT_SECRET_ENTRY:
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
                uint8_t *value =
                    &app->secret_wheels[app->selected_secret_wheel];
                *value = (uint8_t)((*value + 1u) % FV_SECRET_WHEEL_VALUES);
            } else if (event == FV_EVENT_DOWN) {
                uint8_t *value =
                    &app->secret_wheels[app->selected_secret_wheel];
                *value = *value == 0u ? FV_SECRET_WHEEL_VALUES - 1u
                                      : (uint8_t)(*value - 1u);
            } else if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_VAULT_AUTHENTICATING;
                return FV_COMMAND_BEGIN_AUTHENTICATION;
            } else if (event == FV_EVENT_BACK) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_VAULT_AUTHENTICATING:
            if (event == FV_EVENT_AUTH_SUCCEEDED) {
                memset(app->secret_wheels, 0, sizeof(app->secret_wheels));
                app->failed_attempts = 0u;
                app->state = FV_STATE_VAULT_RECORDING_SUCCESS;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_STORE_ATTEMPT_COUNTER;
            }
            if (event == FV_EVENT_AUTH_FAILED) {
                memset(app->secret_wheels, 0, sizeof(app->secret_wheels));
                ++app->failed_attempts;
                app->state = FV_STATE_VAULT_RECORDING_FAILURE;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS |
                       FV_COMMAND_STORE_ATTEMPT_COUNTER;
            }
            break;

        case FV_STATE_VAULT_RECORDING_SUCCESS:
            if (event == FV_EVENT_ATTEMPT_COUNTER_STORED) {
                app->state = FV_STATE_VAULT_UNLOCKED;
                return FV_COMMAND_USB_ATTACH_MSC;
            }
            break;

        case FV_STATE_VAULT_RECORDING_FAILURE:
            if (event == FV_EVENT_ATTEMPT_COUNTER_STORED) {
                if (app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
                    app->state = FV_STATE_DESTROYED;
                    return FV_COMMAND_USB_DETACH |
                           FV_COMMAND_DESTROY_DEVICE_SECRET;
                }
                app->state = FV_STATE_VAULT_SECRET_ENTRY;
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
        case FV_STATE_PROVISIONING:
            snprintf(view->title, sizeof(view->title), "Provisioning");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Follow setup steps");
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
            snprintf(view->lines[0], sizeof(view->lines[0]),
                     app->selected_secret_wheel == 0u
                         ? "[%02u]  %02u   %02u"
                         : app->selected_secret_wheel == 1u
                             ? " %02u  [%02u]  %02u"
                             : " %02u   %02u  [%02u]",
                     (unsigned)app->secret_wheels[0],
                     (unsigned)app->secret_wheels[1],
                     (unsigned)app->secret_wheels[2]);
            snprintf(view->lines[2], sizeof(view->lines[2]), "Failures: %u/%u",
                     (unsigned)app->failed_attempts,
                     (unsigned)FV_MAX_UNLOCK_ATTEMPTS);
            snprintf(view->lines[3], sizeof(view->lines[3]),
                     "Arrows: adjust  OK: submit");
            break;
        case FV_STATE_VAULT_AUTHENTICATING:
            snprintf(view->title, sizeof(view->title), "Unlock vault");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Authenticating...");
            break;
        case FV_STATE_VAULT_RECORDING_SUCCESS:
        case FV_STATE_VAULT_RECORDING_FAILURE:
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
        "provisioning",
        "mode-select",
        "vault-secret-entry",
        "vault-authenticating",
        "vault-recording-success",
        "vault-recording-failure",
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
