#include "fuse_vault/credential_change.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/vault_header_store.h"
#include <string.h>

static void clear(void *data, size_t size) {
    volatile uint8_t *p = data;
    while (size--) *p++ = 0u;
}
static bool random_fill(void *context, uint8_t *out, size_t size) {
    fv_platform_services_t *services = context;
    return services->ops->random_fill(services, out, size);
}
static bool commit_state(fv_platform_services_t *services, fv_security_state_t *state) {
    if (state->sequence == UINT64_MAX) return false;
    ++state->sequence;
    fv_security_state_t verified = {0};
    bool ok = services->ops->store_security_state(services, state) == FV_PERSIST_OK &&
        services->ops->load_security_state(services, &verified) == FV_PERSIST_OK &&
        verified.sequence == state->sequence &&
        verified.failed_attempts == state->failed_attempts &&
        verified.provisioned == state->provisioned &&
        verified.fido_initialized == state->fido_initialized &&
        memcmp(verified.fido_digest, state->fido_digest, 32u) == 0 &&
        verified.header_sequence == state->header_sequence &&
        memcmp(verified.header_tag, state->header_tag, 32u) == 0;
    clear(&verified, sizeof(verified));
    return ok;
}

bool fv_credential_change(const fv_app_t *app, fv_platform_services_t *services,
    const fv_authentication_session_t *session, const fv_credential_costs_t *costs) {
    if (!app || !services || !services->ops || !session || !costs ||
        !services->ops->random_fill || !services->ops->load_vault_header ||
        !services->ops->store_vault_header || !services->ops->read_device_secret ||
        !services->ops->load_security_state || !services->ops->store_security_state ||
        app->state != FV_STATE_CHANGE_SAVING || !app->session_unlocked ||
        !session->vmk_valid || app->failed_attempts != 0u ||
        app->change_secret_entry.method != app->change_entry_method)
        return false;

    fv_secret_encoding_t entry = {0};
    fv_device_secret_t roots = {0};
    fv_vault_header_t header = {0}, verified = {0};
    fv_volume_master_key_t check = {0};
    fv_security_state_t state = {0}, expected = {0};
    fv_secret_method_t method;
    bool ok = false;
    if (!fv_secret_entry_encode(&app->change_secret_entry, &entry) ||
        !fv_entry_method_to_secret_method(app->change_entry_method, &method) ||
        services->ops->read_device_secret(services, &roots) != FV_PERSIST_OK ||
        services->ops->load_vault_header(services, &header) != FV_PERSIST_OK ||
        services->ops->load_security_state(services, &state) != FV_PERSIST_OK ||
        !state.provisioned || state.failed_attempts != 0u ||
        header.sequence == UINT64_MAX) goto cleanup;

    /* Migrate a legacy vault before touching either SD header. A cut here
     * leaves the original credential usable and never advances its revision. */
    if (!fv_vault_header_anchor(&header, &roots, &expected)) goto cleanup;
    if (state.header_sequence == 0u) {
        state.header_sequence = expected.header_sequence;
        memcpy(state.header_tag, expected.header_tag, 32u);
        if (!commit_state(services, &state)) goto cleanup;
    } else if (state.header_sequence != expected.header_sequence ||
               memcmp(state.header_tag, expected.header_tag, 32u) != 0) goto cleanup;

    ++header.sequence;
    header.entry_method = method;
    /* A settings change never silently reduces the existing KDF work factors. */
    fv_credential_costs_t next_costs = *costs;
    if (next_costs.pbkdf2_iterations < header.branch_a_cost)
        next_costs.pbkdf2_iterations = header.branch_a_cost;
    if (next_costs.kmac_iterations < header.branch_b_cost)
        next_costs.kmac_iterations = header.branch_b_cost;
    if (fv_credential_envelope_rewrap(&entry, &roots, &next_costs, random_fill,
            services, &header, &session->vmk) != FV_CREDENTIAL_OK ||
        fv_credential_envelope_open(&entry, &roots, &header, &check) != FV_CREDENTIAL_OK ||
        memcmp(check.bytes, session->vmk.bytes, sizeof(check.bytes)) != 0 ||
        services->ops->store_vault_header(services, &header) != FV_PERSIST_OK)
        goto cleanup;

    /* store_vault_header verifies the staged sector. Publishing the anchor is
     * the sole commit point; regular header loads ignore an uncommitted stage. */
    if (!fv_vault_header_anchor(&header, &roots, &state) ||
        !commit_state(services, &state) ||
        services->ops->load_vault_header(services, &verified) != FV_PERSIST_OK ||
        !fv_vault_header_anchor(&verified, &roots, &expected) ||
        expected.header_sequence != state.header_sequence ||
        memcmp(expected.header_tag, state.header_tag, 32u) != 0)
        goto cleanup;
    ok = true;
cleanup:
    clear(&entry, sizeof(entry));
    clear(&roots, sizeof(roots));
    clear(&header, sizeof(header));
    clear(&verified, sizeof(verified));
    clear(&state, sizeof(state));
    clear(&expected, sizeof(expected));
    fv_volume_master_key_clear(&check);
    return ok;
}
