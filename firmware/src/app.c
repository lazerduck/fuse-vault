#include "fuse_vault/app.h"
#include "fuse_vault/crypto_stack.h"
#include "fuse_vault/secret_input.h"

#include <stdio.h>
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
    app->state = FV_STATE_SETUP_MEDIA_ERROR;
    return FV_COMMAND_USB_DETACH |
           FV_COMMAND_ERASE_TRANSIENT_SECRET |
           FV_COMMAND_ERASE_SESSION_KEYS;
}

static void begin_secret_entry(fv_app_t *app) {
    fv_secret_entry_begin(&app->secret_entry, app->selected_entry_method);
}

static fv_command_set_t leave_sensitive_mode(fv_app_t *app) {
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
            if ((event == FV_EVENT_UP || event == FV_EVENT_DOWN) &&
                app->fido_available) {
                app->selected_mode = app->selected_mode == FV_MODE_VAULT
                    ? FV_MODE_FIDO
                    : FV_MODE_VAULT;
            } else if (event == FV_EVENT_SELECT) {
                if (!app->session_unlocked) {
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
            if (event == FV_EVENT_BACK || event == FV_EVENT_LOCK_REQUESTED ||
                event == FV_EVENT_USB_EJECTED) {
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
        case FV_STATE_BOOT_MEDIA_REQUIRED:
            snprintf(view->title, sizeof(view->title), "Vault media required");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Insert vault SD card");
            snprintf(view->lines[2], sizeof(view->lines[2]), "USB remains locked");
            break;
        case FV_STATE_SETUP_REQUIRED:
            snprintf(view->title, sizeof(view->title), "Setup required");
            snprintf(view->lines[0], sizeof(view->lines[0]), "No vault configured");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Select: begin setup");
            break;
        case FV_STATE_SETUP_MEDIA_CHECKING:
            snprintf(view->title, sizeof(view->title), "Checking SD card");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Reading media state...");
            break;
        case FV_STATE_SETUP_MEDIA_CONFIRM:
            snprintf(view->title, sizeof(view->title), "Initialize SD card?");
            snprintf(view->lines[0], sizeof(view->lines[0]), "ALL CARD DATA WILL BE LOST");
            snprintf(view->lines[2], sizeof(view->lines[2]), "OK: erase and use");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Back: cancel");
            break;
        case FV_STATE_SETUP_MEDIA_INITIALIZING:
            snprintf(view->title, sizeof(view->title), "Preparing SD card");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Do not remove power");
            break;
        case FV_STATE_SETUP_MEDIA_ERROR:
            snprintf(view->title, sizeof(view->title), "SD card unavailable");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Insert/check card");
            snprintf(view->lines[2], sizeof(view->lines[2]), "OK: retry");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Back: cancel");
            break;
        case FV_STATE_SETUP_METHOD_SELECT:
            snprintf(view->title, sizeof(view->title), "Entry method");
            snprintf(view->lines[0], sizeof(view->lines[0]), "> %s",
                     fv_secret_method_name(app->selected_entry_method));
            snprintf(view->lines[1], sizeof(view->lines[1]), "%u of %u",
                     (unsigned)app->selected_entry_method + 1u,
                     (unsigned)FV_ENTRY_METHOD_COUNT);
            snprintf(view->lines[3], sizeof(view->lines[3]), "Up/down; OK choose");
            break;
        case FV_STATE_SETUP_SECRET_ENTRY:
            snprintf(view->title, sizeof(view->title), "Create secret");
            fv_secret_entry_render(&app->secret_entry, view);
            break;
        case FV_STATE_SETUP_SECRET_CONFIRM:
            snprintf(view->title, sizeof(view->title), "Confirm secret");
            fv_secret_entry_render(&app->secret_entry, view);
            snprintf(view->lines[3], sizeof(view->lines[3]), "Enter it again");
            break;
        case FV_STATE_SETUP_SECRET_MISMATCH:
            snprintf(view->title, sizeof(view->title), "Secrets differ");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Nothing was saved");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Enter a new secret");
            snprintf(view->lines[3], sizeof(view->lines[3]), "OK: try again");
            break;
        case FV_STATE_SETUP_STACK_SELECT:
            snprintf(view->title, sizeof(view->title), "Encryption stack");
            snprintf(view->lines[0], sizeof(view->lines[0]), "> %s",
                     fv_crypto_stack_preset_name(app->selected_stack_preset));
            snprintf(view->lines[1], sizeof(view->lines[1]), "%u of %u",
                     (unsigned)app->selected_stack_preset + 1u,
                     (unsigned)fv_crypto_stack_preset_count());
            snprintf(view->lines[3], sizeof(view->lines[3]), "Up/down; OK choose");
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
            snprintf(view->lines[1], sizeof(view->lines[1]), "%s FIDO2 %s",
                     app->selected_mode == FV_MODE_FIDO ? ">" : " ",
                     app->fido_available ? "" : "(planned)");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Up/down + select");
            break;
        case FV_STATE_VAULT_SECRET_ENTRY:
            snprintf(view->title, sizeof(view->title), "Unlock device");
            fv_secret_entry_render(&app->secret_entry, view);
            snprintf(view->lines[3], sizeof(view->lines[3]), "Failures: %u/%u",
                     (unsigned)app->failed_attempts,
                     (unsigned)FV_MAX_UNLOCK_ATTEMPTS);
            break;
        case FV_STATE_VAULT_RESERVING_ATTEMPT:
            snprintf(view->title, sizeof(view->title), "Unlock device");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Reserving attempt...");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Do not remove power");
            break;
        case FV_STATE_VAULT_AUTHENTICATING:
            snprintf(view->title, sizeof(view->title), "Unlock device");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Authenticating...");
            break;
        case FV_STATE_VAULT_RECORDING_SUCCESS:
            snprintf(view->title, sizeof(view->title), "Unlock device");
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
            snprintf(view->lines[0], sizeof(view->lines[0]), app->fido_waiting ? (app->fido_reset_pending ? "Erase all FIDO credentials?" : "Approve FIDO request?") : "Ready for authentication");
            snprintf(view->lines[1], sizeof(view->lines[1]), app->fido_waiting ? "Select: approve" : "Device unlock verified");
            snprintf(view->lines[3], sizeof(view->lines[3]), app->fido_waiting ? "Back: cancel" : "Back: lock");
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
    if (view->secret_controls && strcmp(view->select_action, "Done") == 0) {
        snprintf(view->select_action, sizeof(view->select_action), "%s",
                 app->state == FV_STATE_SETUP_SECRET_ENTRY ? "Next" :
                 app->state == FV_STATE_SETUP_SECRET_CONFIRM ? "Confirm" : "Unlock");
    }

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
        "destroyed",
        "fault",
    };

    if ((unsigned)state >= (sizeof(names) / sizeof(names[0]))) {
        return "unknown";
    }
    return names[state];
}
