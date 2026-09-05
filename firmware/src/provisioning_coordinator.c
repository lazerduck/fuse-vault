#include "fuse_vault/provisioning_coordinator.h"

#include "fuse_vault/secret_input.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool all_zero(const uint8_t *data, size_t length) {
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= data[index];
    return combined == 0u;
}

static bool services_valid(const fv_platform_services_t *services) {
    return services != NULL && services->ops != NULL &&
           services->ops->random_fill != NULL &&
           services->ops->device_secret_status != NULL &&
           services->ops->provision_device_secret != NULL &&
           services->ops->read_device_secret != NULL &&
           services->ops->revoke_device_secret != NULL &&
           services->ops->load_security_state != NULL &&
           services->ops->store_security_state != NULL &&
           services->ops->load_vault_header != NULL &&
           services->ops->store_vault_header != NULL;
}

static bool random_adapter(void *context, uint8_t *output, size_t length) {
    fv_platform_services_t *services = context;
    return services->ops->random_fill(services, output, length);
}

static bool header_matches(const fv_vault_header_t *left,
                           const fv_vault_header_t *right) {
    return left->sequence == right->sequence &&
           left->crypto_profile == right->crypto_profile &&
           left->entry_method == right->entry_method &&
           left->branch_a_cost == right->branch_a_cost &&
           left->branch_b_cost == right->branch_b_cost &&
           left->wrapped_vmk_length == right->wrapped_vmk_length &&
           memcmp(left->vault_id, right->vault_id, FV_VAULT_ID_SIZE) == 0 &&
           memcmp(left->branch_a_salt, right->branch_a_salt, FV_SALT_SIZE) == 0 &&
           memcmp(left->branch_b_salt, right->branch_b_salt, FV_SALT_SIZE) == 0 &&
           memcmp(left->wrapped_vmk, right->wrapped_vmk,
                  FV_WRAPPED_VMK_CAPACITY) == 0;
}

static bool state_matches(const fv_security_state_t *left,
                          const fv_security_state_t *right) {
    return left->sequence == right->sequence &&
           left->failed_attempts == right->failed_attempts &&
           left->provisioned == right->provisioned;
}

fv_setup_provision_result_t fv_setup_provision(
    const fv_app_t *app, fv_platform_services_t *services,
    const fv_credential_costs_t *costs,
    fv_setup_provision_workspace_t *workspace) {
    if (workspace == NULL) return FV_SETUP_PROVISION_INVALID_ARGUMENT;
    secure_clear(workspace, sizeof(*workspace));
    fv_setup_provision_result_t result = FV_SETUP_PROVISION_INVALID_ARGUMENT;
    bool roots_active = false;

    if (app == NULL || costs == NULL || !services_valid(services) ||
        app->state != FV_STATE_PROVISIONING ||
        !fv_entry_method_valid(app->selected_entry_method) ||
        app->setup_secret_entry.method != app->selected_entry_method ||
        costs->pbkdf2_iterations == 0u ||
        costs->pbkdf2_iterations > FV_CREDENTIAL_PBKDF2_MAX_ITERATIONS ||
        costs->kmac_iterations == 0u ||
        costs->kmac_iterations > FV_CREDENTIAL_KMAC_MAX_ITERATIONS) {
        goto cleanup;
    }

    fv_device_secret_status_t roots_status = FV_DEVICE_SECRET_INVALID;
    if (services->ops->device_secret_status(services, &roots_status) !=
            FV_PERSIST_OK) {
        result = FV_SETUP_PROVISION_ROOTS_FAILED;
        goto cleanup;
    }
    fv_security_state_t existing_state;
    fv_vault_header_t existing_header;
    const fv_persist_result_t state_status =
        services->ops->load_security_state(services, &existing_state);
    const fv_persist_result_t header_status =
        services->ops->load_vault_header(services, &existing_header);
    secure_clear(&existing_state, sizeof(existing_state));
    secure_clear(&existing_header, sizeof(existing_header));
    if (roots_status != FV_DEVICE_SECRET_EMPTY ||
        state_status != FV_PERSIST_NOT_FOUND ||
        header_status != FV_PERSIST_NOT_FOUND) {
        result = (state_status == FV_PERSIST_IO_ERROR ||
                  header_status == FV_PERSIST_IO_ERROR)
            ? FV_SETUP_PROVISION_VERIFICATION_FAILED
            : FV_SETUP_PROVISION_NOT_PRISTINE;
        goto cleanup;
    }

    if (!fv_setup_secret_encode(app, &workspace->encoding)) {
        result = FV_SETUP_PROVISION_ENCODING_FAILED;
        goto cleanup;
    }
    fv_secret_method_t persisted_method;
    if (!fv_entry_method_to_secret_method(app->selected_entry_method,
                                           &persisted_method)) {
        result = FV_SETUP_PROVISION_ENCODING_FAILED;
        goto cleanup;
    }
    if (!services->ops->random_fill(
            services, workspace->generated_roots.device_secret,
            sizeof(workspace->generated_roots.device_secret)) ||
        all_zero(workspace->generated_roots.device_secret,
                 FV_DEVICE_ROOT_SIZE) ||
        all_zero(workspace->generated_roots.device_secret + FV_DEVICE_ROOT_SIZE,
                 FV_DEVICE_ROOT_SIZE) ||
        !services->ops->random_fill(services, workspace->header.vault_id,
                                    FV_VAULT_ID_SIZE) ||
        all_zero(workspace->header.vault_id, FV_VAULT_ID_SIZE)) {
        result = FV_SETUP_PROVISION_RANDOM_FAILED;
        goto cleanup;
    }
    workspace->header.sequence = 1u;
    workspace->header.entry_method = persisted_method;

    if (services->ops->provision_device_secret(
            services, &workspace->generated_roots) != FV_PERSIST_OK) {
        result = FV_SETUP_PROVISION_ROOTS_FAILED;
        /* A lost acknowledgement can report failure after committing roots. */
        roots_status = FV_DEVICE_SECRET_INVALID;
        if (services->ops->device_secret_status(services, &roots_status) !=
                FV_PERSIST_OK || roots_status != FV_DEVICE_SECRET_EMPTY) {
            roots_active = true;
        }
        goto cleanup;
    }
    roots_active = true;
    if (services->ops->read_device_secret(
            services, &workspace->stored_roots) != FV_PERSIST_OK ||
        memcmp(&workspace->stored_roots, &workspace->generated_roots,
               sizeof(workspace->stored_roots)) != 0) {
        result = FV_SETUP_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    if (fv_credential_envelope_create(
            &workspace->encoding, &workspace->stored_roots, costs,
            random_adapter, services, &workspace->header, &workspace->vmk) !=
        FV_CREDENTIAL_OK) {
        result = FV_SETUP_PROVISION_ENVELOPE_FAILED;
        goto cleanup;
    }
    if (services->ops->store_vault_header(services, &workspace->header) !=
        FV_PERSIST_OK) {
        result = FV_SETUP_PROVISION_HEADER_FAILED;
        goto cleanup;
    }
    if (services->ops->load_vault_header(
            services, &workspace->verified_header) != FV_PERSIST_OK ||
        !header_matches(&workspace->header, &workspace->verified_header)) {
        result = FV_SETUP_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }

    workspace->state = (fv_security_state_t) {
        .sequence = 1u,
        .failed_attempts = 0u,
        .provisioned = true,
    };
    if (services->ops->store_security_state(services, &workspace->state) !=
        FV_PERSIST_OK) {
        result = FV_SETUP_PROVISION_STATE_FAILED;
        goto cleanup;
    }
    if (services->ops->load_security_state(
            services, &workspace->verified_state) != FV_PERSIST_OK ||
        !state_matches(&workspace->state, &workspace->verified_state)) {
        result = FV_SETUP_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    result = FV_SETUP_PROVISION_OK;

cleanup:
    if (result != FV_SETUP_PROVISION_OK && roots_active) {
        (void)services->ops->revoke_device_secret(services);
    }
    secure_clear(workspace, sizeof(*workspace));
    return result;
}
