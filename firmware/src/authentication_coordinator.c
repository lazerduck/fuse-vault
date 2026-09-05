#include "fuse_vault/authentication_coordinator.h"

#include "fuse_vault/secret_input.h"

#include <stddef.h>
#include <stdint.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

void fv_authentication_session_clear(fv_authentication_session_t *session) {
    if (session != NULL) secure_clear(session, sizeof(*session));
}

static bool services_valid(const fv_platform_services_t *services) {
    return services != NULL && services->ops != NULL &&
           services->ops->read_device_secret != NULL &&
           services->ops->load_vault_header != NULL;
}

fv_authenticate_result_t fv_authenticate(
    const fv_app_t *app, fv_platform_services_t *services,
    fv_authentication_session_t *session,
    fv_authentication_workspace_t *workspace) {
    if (session == NULL) return FV_AUTHENTICATE_FATAL;
    fv_authentication_session_clear(session);
    if (workspace == NULL) return FV_AUTHENTICATE_FATAL;
    secure_clear(workspace, sizeof(*workspace));
    fv_authenticate_result_t result = FV_AUTHENTICATE_FATAL;

    /* This state proves FV_COMMAND_STORE_ATTEMPT_COUNTER completed first. */
    if (app == NULL || !services_valid(services) ||
        app->state != FV_STATE_VAULT_AUTHENTICATING ||
        app->failed_attempts == 0u ||
        !fv_secret_input_encode(app, &workspace->encoding)) {
        goto cleanup;
    }
    if (services->ops->load_vault_header(services, &workspace->header) !=
            FV_PERSIST_OK ||
        !fv_vault_header_valid(&workspace->header)) {
        goto cleanup;
    }

    fv_secret_method_t entered_method;
    if (!fv_entry_method_to_secret_method(app->secret_entry.method,
                                           &entered_method) ||
        entered_method != workspace->header.entry_method ||
        app->secret_entry.method != app->selected_entry_method) {
        result = FV_AUTHENTICATE_REJECTED;
        goto cleanup;
    }
    if (services->ops->read_device_secret(services, &workspace->roots) !=
        FV_PERSIST_OK) {
        goto cleanup;
    }

    const fv_credential_result_t open_result = fv_credential_envelope_open(
        &workspace->encoding, &workspace->roots, &workspace->header,
        &workspace->candidate_vmk);
    if (open_result == FV_CREDENTIAL_OK) {
        session->vmk = workspace->candidate_vmk;
        session->vmk_valid = true;
        result = FV_AUTHENTICATE_OK;
    } else if (open_result == FV_CREDENTIAL_AUTHENTICATION_FAILED) {
        result = FV_AUTHENTICATE_REJECTED;
    }

cleanup:
    secure_clear(workspace, sizeof(*workspace));
    if (result != FV_AUTHENTICATE_OK) fv_authentication_session_clear(session);
    return result;
}
