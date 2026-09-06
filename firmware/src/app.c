#include "fuse_vault/app.h"
#include "fuse_vault/crypto_stack.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/settings.h"

#include <string.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static void begin_secret_entry(fv_app_t *app);

static fv_command_set_t enter_fault(fv_app_t *app) {
    app->session_unlocked = false;
    fv_secret_entry_clear(&app->secret_entry);
    fv_secret_entry_clear(&app->setup_secret_entry);
    fv_settings_clear(app);
    app->state = FV_STATE_FAULT;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

static fv_command_set_t complete_boot(fv_app_t *app) {
    if (app->provisioned && app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
        app->state = FV_STATE_DESTROYED;
        return FV_COMMAND_USB_DETACH |
               FV_COMMAND_ERASE_TRANSIENT_SECRET |
               FV_COMMAND_ERASE_SESSION_KEYS |
               FV_COMMAND_DESTROY_DEVICE_SECRET;
    }
    app->state = app->provisioned ? FV_STATE_MODE_SELECT
                                  : FV_STATE_SETUP_REQUIRED;
    if (app->provisioned && app->fido_available) {
        begin_secret_entry(app);
        app->state = FV_STATE_VAULT_SECRET_ENTRY;
    }
    return FV_COMMAND_NONE;
}

static fv_command_set_t media_lost_during_setup(fv_app_t *app) {
    fv_secret_entry_clear(&app->secret_entry);
    fv_secret_entry_clear(&app->setup_secret_entry);
    fv_settings_clear(app);
    app->state = FV_STATE_SETUP_MEDIA_ERROR;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

static void begin_secret_entry(fv_app_t *app) {
    fv_secret_entry_begin(&app->secret_entry, app->selected_entry_method);
}

static fv_command_set_t leave_sensitive_mode(fv_app_t *app) {
    fv_settings_clear(app);
    fv_secret_entry_clear(&app->secret_entry);
    app->session_unlocked = false;
    app->state = FV_STATE_MODE_SELECT;
    if (app->fido_available) {
        begin_secret_entry(app);
        app->state = FV_STATE_VAULT_SECRET_ENTRY;
    }
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

void fv_app_init(fv_app_t *app, bool provisioned, uint8_t persisted_failed_attempts,
                 fv_entry_method_t entry_method) {
    if (app == NULL) {
        return;
    }

    *app = (fv_app_t) {
        .state = FV_STATE_BOOTING,
        .selected_mode = FV_MODE_VAULT,
        .selected_entry_method = (unsigned)entry_method < FV_ENTRY_METHOD_COUNT
            ? entry_method : FV_ENTRY_METHOD_WHEELS,
        .failed_attempts = persisted_failed_attempts > FV_MAX_UNLOCK_ATTEMPTS
            ? FV_MAX_UNLOCK_ATTEMPTS
            : persisted_failed_attempts,
        .provisioned = provisioned,
    };
    fv_crypto_stack_default(&app->selected_encryption_stack);
}

void fv_app_set_fido_available(fv_app_t *app, bool available) {
    if (app == NULL) return;
    app->fido_available = available;
    if (!available && app->selected_mode == FV_MODE_FIDO) {
        app->selected_mode = FV_MODE_VAULT;
    }
}

fv_command_set_t fv_app_handle(fv_app_t *app, fv_event_t event) {
    if (app == NULL) {
        return FV_COMMAND_NONE;
    }

    if (event == FV_EVENT_FATAL_ERROR) {
        return enter_fault(app);
    }
    if (event == FV_EVENT_STORAGE_FAILED) {
        if (!app->provisioned &&
            app->state >= FV_STATE_SETUP_REQUIRED &&
            app->state < FV_STATE_PROVISIONING) {
            return media_lost_during_setup(app);
        }
        return enter_fault(app);
    }

    if (fv_settings_owns_state(app->state)) {
        if (event == FV_EVENT_LOCK_REQUESTED || event == FV_EVENT_USB_EJECTED ||
            (app->state == FV_STATE_SETTINGS && event == FV_EVENT_BACK) ||
            (app->state == FV_STATE_CHANGE_SAVED &&
             (event == FV_EVENT_SELECT || event == FV_EVENT_BACK))) {
            app->selected_mode = FV_MODE_VAULT;
            return leave_sensitive_mode(app);
        }
        if (!app->session_unlocked && app->state != FV_STATE_CHANGE_SAVED)
            return enter_fault(app);
        return fv_settings_handle(app, event);
    }

    switch (app->state) {
        case FV_STATE_BOOTING:
            if (event == FV_EVENT_BOOT_MEDIA_REQUIRED && app->provisioned) {
                app->state = FV_STATE_BOOT_MEDIA_REQUIRED;
            } else if (event == FV_EVENT_BOOT_COMPLETED) {
                return complete_boot(app);
            }
            break;

        case FV_STATE_BOOT_MEDIA_REQUIRED:
            if (event == FV_EVENT_BOOT_COMPLETED) {
                return complete_boot(app);
            }
            break;

        case FV_STATE_SETUP_REQUIRED:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_SETUP_MEDIA_CHECKING;
                return FV_COMMAND_INSPECT_MEDIA;
            }
            break;

        case FV_STATE_SETUP_MEDIA_CHECKING:
            if (event == FV_EVENT_MEDIA_FOUND) {
                app->state = FV_STATE_SETUP_MEDIA_CONFIRM;
            } else if (event == FV_EVENT_MEDIA_FAILED) {
                app->state = FV_STATE_SETUP_MEDIA_ERROR;
            }
            break;

        case FV_STATE_SETUP_MEDIA_CONFIRM:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_SETUP_MEDIA_INITIALIZING;
                return FV_COMMAND_PREPARE_MEDIA;
            }
            if (event == FV_EVENT_BACK) app->state = FV_STATE_SETUP_REQUIRED;
            break;

        case FV_STATE_SETUP_MEDIA_INITIALIZING:
            if (event == FV_EVENT_MEDIA_PREPARED) {
                app->state = FV_STATE_SETUP_METHOD_SELECT;
            } else if (event == FV_EVENT_MEDIA_FAILED) {
                app->state = FV_STATE_SETUP_MEDIA_ERROR;
            }
            break;

        case FV_STATE_SETUP_MEDIA_ERROR:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_SETUP_MEDIA_CHECKING;
                return FV_COMMAND_INSPECT_MEDIA;
            }
            if (event == FV_EVENT_BACK) app->state = FV_STATE_SETUP_REQUIRED;
            break;

        case FV_STATE_SETUP_METHOD_SELECT:
            if (event == FV_EVENT_UP) {
                app->selected_entry_method = app->selected_entry_method == 0
                    ? (fv_entry_method_t)(FV_ENTRY_METHOD_COUNT - 1)
                    : (fv_entry_method_t)(app->selected_entry_method - 1);
            } else if (event == FV_EVENT_DOWN) {
                app->selected_entry_method = (fv_entry_method_t)(
                    (app->selected_entry_method + 1) % FV_ENTRY_METHOD_COUNT);
            } else if (event == FV_EVENT_SELECT) {
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            } else if (event == FV_EVENT_BACK) {
                app->state = FV_STATE_SETUP_REQUIRED;
            }
            break;

        case FV_STATE_SETUP_SECRET_ENTRY: {
            const fv_secret_event_result_t result =
                fv_secret_entry_handle(&app->secret_entry, event);
            if (result == FV_SECRET_EVENT_COMPLETE) {
                app->setup_secret_entry = app->secret_entry;
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_CONFIRM;
            } else if (event == FV_EVENT_BACK && result == FV_SECRET_EVENT_IGNORED) {
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_METHOD_SELECT;
            }
            break;
        }

        case FV_STATE_SETUP_SECRET_CONFIRM: {
            const fv_secret_event_result_t result =
                fv_secret_entry_handle(&app->secret_entry, event);
            if (result == FV_SECRET_EVENT_COMPLETE) {
                fv_secret_encoding_t entered;
                fv_secret_encoding_t expected;
                const bool matches =
                    fv_secret_entry_encode(&app->secret_entry, &entered) &&
                    fv_secret_entry_encode(&app->setup_secret_entry, &expected) &&
                    memcmp(&entered, &expected, sizeof(entered)) == 0;
                secure_clear(&entered, sizeof(entered));
                secure_clear(&expected, sizeof(expected));
                if (matches) {
                    fv_secret_entry_clear(&app->secret_entry);
                    app->state = FV_STATE_SETUP_STACK_SELECT;
                } else {
                    fv_secret_entry_clear(&app->secret_entry);
                    fv_secret_entry_clear(&app->setup_secret_entry);
                    app->state = FV_STATE_SETUP_SECRET_MISMATCH;
                }
            } else if (event == FV_EVENT_BACK && result == FV_SECRET_EVENT_IGNORED) {
                fv_secret_entry_clear(&app->setup_secret_entry);
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;
        }

        case FV_STATE_SETUP_SECRET_MISMATCH:
            if (event == FV_EVENT_SELECT || event == FV_EVENT_BACK) {
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_SETUP_STACK_SELECT:
            if (event == FV_EVENT_UP) {
                app->selected_stack_preset = app->selected_stack_preset == 0u
                    ? (uint8_t)(fv_crypto_stack_preset_count() - 1u)
                    : (uint8_t)(app->selected_stack_preset - 1u);
            } else if (event == FV_EVENT_DOWN) {
                app->selected_stack_preset = (uint8_t)(
                    (app->selected_stack_preset + 1u) %
                    fv_crypto_stack_preset_count());
            } else if (event == FV_EVENT_SELECT) {
                if (!fv_crypto_stack_preset(app->selected_stack_preset,
                                            &app->selected_encryption_stack)) {
                    return enter_fault(app);
                }
                app->state = FV_STATE_SETUP_POLICY_CONFIRM;
            } else if (event == FV_EVENT_BACK) {
                fv_secret_entry_clear(&app->setup_secret_entry);
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_SETUP_POLICY_CONFIRM:
            if (event == FV_EVENT_SELECT) {
                app->state = FV_STATE_PROVISIONING;
                return FV_COMMAND_BEGIN_PROVISIONING;
            }
            if (event == FV_EVENT_BACK) {
                fv_secret_entry_clear(&app->setup_secret_entry);
                begin_secret_entry(app);
                app->state = FV_STATE_SETUP_SECRET_ENTRY;
            }
            break;

        case FV_STATE_PROVISIONING:
            if (event == FV_EVENT_PROVISIONING_SUCCEEDED) {
                fv_secret_entry_clear(&app->setup_secret_entry);
                app->provisioned = true;
                app->failed_attempts = 0u;
                app->state = FV_STATE_MODE_SELECT;
                if (app->fido_available) {
                    begin_secret_entry(app);
                    app->state = FV_STATE_VAULT_SECRET_ENTRY;
                }
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            if (event == FV_EVENT_PROVISIONING_FAILED) {
                fv_secret_entry_clear(&app->setup_secret_entry);
                app->state = FV_STATE_SETUP_REQUIRED;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_ERASE_SESSION_KEYS;
            }
            break;

        case FV_STATE_MODE_SELECT:
            if (event == FV_EVENT_UP || event == FV_EVENT_DOWN) {
                do {
                    app->selected_mode = (fv_mode_t)((app->selected_mode +
                        (event == FV_EVENT_UP ? FV_MODE_COUNT - 1 : 1)) % FV_MODE_COUNT);
                } while (app->selected_mode == FV_MODE_FIDO && !app->fido_available);
            } else if (event == FV_EVENT_SELECT) {
                if (app->selected_mode == FV_MODE_SETTINGS) {
                    /* Fresh normal authentication; settings never attach a USB interface. */
                    app->session_unlocked = false;
                    fv_settings_clear(app);
                    begin_secret_entry(app);
                    app->state = FV_STATE_VAULT_SECRET_ENTRY;
                    return FV_COMMAND_USB_DETACH | FV_COMMAND_ERASE_SESSION_KEYS;
                } else if (!app->session_unlocked) {
                    begin_secret_entry(app);
                    app->state = FV_STATE_VAULT_SECRET_ENTRY;
                } else if (app->selected_mode == FV_MODE_VAULT) {
                    app->state = FV_STATE_VAULT_UNLOCKED;
                    return FV_COMMAND_USB_ATTACH_MSC;
                } else if (app->fido_available) {
                    app->state = FV_STATE_FIDO_READY;
                    return FV_COMMAND_USB_ATTACH_FIDO;
                }
            } else if (event == FV_EVENT_BACK || event == FV_EVENT_LOCK_REQUESTED) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_VAULT_SECRET_ENTRY: {
            const fv_secret_event_result_t result =
                fv_secret_entry_handle(&app->secret_entry, event);
            if (result == FV_SECRET_EVENT_COMPLETE) {
                if (app->failed_attempts >= FV_MAX_UNLOCK_ATTEMPTS) {
                    fv_secret_entry_clear(&app->secret_entry);
                    app->state = FV_STATE_DESTROYED;
                    return FV_COMMAND_USB_DETACH |
                           FV_COMMAND_ERASE_TRANSIENT_SECRET |
                           FV_COMMAND_ERASE_SESSION_KEYS |
                           FV_COMMAND_DESTROY_DEVICE_SECRET;
                }
                ++app->failed_attempts;
                app->state = FV_STATE_VAULT_RESERVING_ATTEMPT;
                return FV_COMMAND_STORE_ATTEMPT_COUNTER;
            } else if (event == FV_EVENT_BACK && result == FV_SECRET_EVENT_IGNORED) {
                return leave_sensitive_mode(app);
            }
            break;
        }

        case FV_STATE_VAULT_RESERVING_ATTEMPT:
            if (event == FV_EVENT_ATTEMPT_COUNTER_STORED) {
                app->state = FV_STATE_VAULT_AUTHENTICATING;
                return FV_COMMAND_BEGIN_AUTHENTICATION;
            }
            break;

        case FV_STATE_VAULT_AUTHENTICATING:
            if (event == FV_EVENT_AUTH_SUCCEEDED) {
                fv_secret_entry_clear(&app->secret_entry);
                app->failed_attempts = 0u;
                app->state = FV_STATE_VAULT_RECORDING_SUCCESS;
                return FV_COMMAND_ERASE_TRANSIENT_SECRET |
                       FV_COMMAND_STORE_ATTEMPT_COUNTER;
            }
            if (event == FV_EVENT_AUTH_FAILED) {
                begin_secret_entry(app);
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
                app->session_unlocked = true;
                if (app->selected_mode == FV_MODE_SETTINGS) {
                    app->selected_setting = 0u;
                    app->state = FV_STATE_SETTINGS;
                    return FV_COMMAND_NONE;
                }
                if (app->fido_available) {
                    app->state = FV_STATE_MODE_SELECT;
                    return FV_COMMAND_NONE;
                }
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
            if (event == FV_EVENT_SELECT && app->session_unlocked && !app->fido_waiting) {
                app->state = FV_STATE_PASSKEY_LIST;
                app->passkey_index = app->passkey_offset = 0;
                return FV_COMMAND_PASSKEY_BEGIN;
            }
            if (event == FV_EVENT_BACK || event == FV_EVENT_LOCK_REQUESTED ||
                event == FV_EVENT_USB_EJECTED) {
                return leave_sensitive_mode(app);
            }
            break;

        case FV_STATE_PASSKEY_LIST:
        case FV_STATE_PASSKEY_DELETE_CONFIRM:
            if (event == FV_EVENT_LOCK_REQUESTED || event == FV_EVENT_USB_EJECTED)
                return leave_sensitive_mode(app);
            if (app->state == FV_STATE_PASSKEY_DELETE_CONFIRM) {
                if (event == FV_EVENT_BACK) app->state = FV_STATE_PASSKEY_LIST;
                if (event == FV_EVENT_RIGHT && app->passkey_count) {
                    app->state = FV_STATE_PASSKEY_LIST;
                    app->passkey_offset = 0;
                    return FV_COMMAND_PASSKEY_DELETE;
                }
                break;
            }
            if (event == FV_EVENT_BACK) {
                app->state = FV_STATE_FIDO_READY;
                return FV_COMMAND_PASSKEY_END;
            }
            if (event == FV_EVENT_SELECT && app->passkey_count) {
                app->state = FV_STATE_PASSKEY_DELETE_CONFIRM;
            } else if ((event == FV_EVENT_UP || event == FV_EVENT_DOWN) && app->passkey_count) {
                if (event == FV_EVENT_UP)
                    app->passkey_index = (uint16_t)(app->passkey_index ? app->passkey_index - 1u : app->passkey_count - 1u);
                else app->passkey_index = (uint16_t)((app->passkey_index + 1u) % app->passkey_count);
                app->passkey_offset = 0;
                return FV_COMMAND_PASSKEY_READ;
            } else if (event == FV_EVENT_LEFT && app->passkey_offset) {
                app->passkey_offset--;
            } else if (event == FV_EVENT_RIGHT && app->passkey_offset < 255u &&
                (strlen(app->passkey.site) > app->passkey_offset + 24u ||
                 strlen(app->passkey.account) > app->passkey_offset + 24u)) {
                app->passkey_offset++;
            }
            break;

        case FV_STATE_SETTINGS:
        case FV_STATE_CHANGE_METHOD:
        case FV_STATE_CHANGE_SECRET:
        case FV_STATE_CHANGE_CONFIRM:
        case FV_STATE_CHANGE_MISMATCH:
        case FV_STATE_CHANGE_REVIEW:
        case FV_STATE_CHANGE_SAVING:
        case FV_STATE_CHANGE_SAVED:
        case FV_STATE_DESTROYED:
        case FV_STATE_FAULT:
            break;
    }

    return FV_COMMAND_NONE;
}

const char *fv_state_name(fv_state_t state) {
    static const char *const names[] = {
        "booting",
        "boot-media-required",
        "setup-required",
        "setup-media-checking",
        "setup-media-confirm",
        "setup-media-initializing",
        "setup-media-error",
        "setup-method-select",
        "setup-secret-entry",
        "setup-secret-confirm",
        "setup-secret-mismatch",
        "setup-stack-select",
        "setup-policy-confirm",
        "provisioning",
        "mode-select",
        "vault-secret-entry",
        "vault-reserving-attempt",
        "vault-authenticating",
        "vault-recording-success",
        "vault-unlocked",
        "fido-ready",
        "passkey-list",
        "passkey-delete-confirm",
        "settings",
        "change-method",
        "change-secret",
        "change-confirm",
        "change-mismatch",
        "change-review",
        "change-saving",
        "change-saved",
        "destroyed",
        "fault",
    };

    if ((unsigned)state >= (sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[state];
}
