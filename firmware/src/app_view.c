#include "fuse_vault/app.h"
#include "fuse_vault/crypto_stack.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/settings.h"
#include <stdio.h>
#include <string.h>

static void clear_view(fv_ui_view_t *view) {
    memset(view, 0, sizeof(*view));
}

void fv_app_render(const fv_app_t *app, fv_ui_view_t *view) {
    if (app == NULL || view == NULL) {
        return;
    }

    clear_view(view);
    if (fv_settings_owns_state(app->state)) {
        fv_settings_render(app, view);
        return;
    }

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
            snprintf(view->lines[2], sizeof(view->lines[2]), "%s Settings",
                     app->selected_mode == FV_MODE_SETTINGS ? ">" : " ");
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
            snprintf(view->lines[1], sizeof(view->lines[1]), app->fido_waiting ? "Select: approve" : "Select: passkeys");
            snprintf(view->lines[3], sizeof(view->lines[3]), app->fido_waiting ? "Back: cancel" : "Back: lock");
            break;
        case FV_STATE_PASSKEY_LIST:
        case FV_STATE_PASSKEY_DELETE_CONFIRM: {
            snprintf(view->title, sizeof(view->title), app->state == FV_STATE_PASSKEY_LIST
                ? "Passkeys %u/%u" : "Delete passkey %u/%u?",
                app->passkey_count ? app->passkey_index + 1u : 0u, app->passkey_count);
            size_t site_offset = app->passkey_offset < strlen(app->passkey.site) ? app->passkey_offset : strlen(app->passkey.site);
            size_t account_offset = app->passkey_offset < strlen(app->passkey.account) ? app->passkey_offset : strlen(app->passkey.account);
            snprintf(view->lines[0], sizeof(view->lines[0]), "%.24s", app->passkey_count ? app->passkey.site + site_offset : "No saved passkeys");
            snprintf(view->lines[1], sizeof(view->lines[1]), "%.24s", app->passkey.account + account_offset);
            snprintf(view->lines[2], sizeof(view->lines[2]), app->state == FV_STATE_PASSKEY_LIST ? "Up/down: browse L/R: text" : "Right: delete permanently");
            snprintf(view->lines[3], sizeof(view->lines[3]), app->state == FV_STATE_PASSKEY_LIST ? "Select: delete Back: exit" : "Back: cancel");
            break;
        }
        case FV_STATE_SETTINGS:
        case FV_STATE_CHANGE_METHOD:
        case FV_STATE_CHANGE_SECRET:
        case FV_STATE_CHANGE_CONFIRM:
        case FV_STATE_CHANGE_MISMATCH:
        case FV_STATE_CHANGE_REVIEW:
        case FV_STATE_CHANGE_SAVING:
        case FV_STATE_CHANGE_SAVED:
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

