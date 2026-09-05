#include "fuse_vault/device_provisioning.h"

#include "fuse_vault/journal_authenticator.h"

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

static bool state_matches(const fv_journal_state_t *left,
                          const fv_journal_state_t *right) {
    return left->sequence == right->sequence &&
           left->previous_sequence == right->previous_sequence &&
           left->failed_attempts == right->failed_attempts &&
           left->provisioned == right->provisioned &&
           memcmp(left->vault_id, right->vault_id, FV_VAULT_ID_SIZE) == 0;
}

static bool erase_journal(fv_journal_flash_t *flash) {
    return flash != NULL && flash->ops != NULL && flash->ops->erase != NULL &&
           flash->erase_block_size != 0u && flash->size != 0u &&
           flash->size % flash->erase_block_size == 0u &&
           flash->ops->erase(flash, 0u, flash->size);
}

fv_provision_result_t fv_device_provision(
    const fv_device_provisioning_t *provisioning,
    fv_journal_state_t *initial_state) {
    if (provisioning == NULL || initial_state == NULL ||
        provisioning->roots_storage == NULL ||
        provisioning->journal_flash == NULL ||
        provisioning->device_context == NULL ||
        provisioning->device_context_length != FV_VAULT_ID_SIZE ||
        provisioning->random_fill == NULL) {
        return FV_PROVISION_INVALID_ARGUMENT;
    }
    memset(initial_state, 0, sizeof(*initial_state));
    const fv_device_roots_result_t roots_status =
        fv_device_roots_status(provisioning->roots_storage);
    if (roots_status == FV_DEVICE_ROOTS_IO_ERROR) {
        return FV_PROVISION_ROOTS_FAILED;
    }
    if (roots_status != FV_DEVICE_ROOTS_EMPTY) {
        return FV_PROVISION_NOT_EMPTY;
    }

    fv_device_secret_t roots;
    fv_journal_state_t created = {
        .sequence = 1u,
        .previous_sequence = 0u,
        .failed_attempts = 0u,
        .provisioned = true,
    };
    fv_dual_journal_authenticator_t authenticator;
    fv_security_journal_t journal;
    fv_provision_result_t result = FV_PROVISION_RANDOM_FAILED;
    bool authenticator_ready = false;

    if (!provisioning->random_fill(provisioning->random_context,
                                   roots.device_secret,
                                   sizeof(roots.device_secret)) ||
        !provisioning->random_fill(provisioning->random_context,
                                   created.vault_id,
                                   sizeof(created.vault_id)) ||
        all_zero(roots.device_secret, FV_DEVICE_ROOT_SIZE) ||
        all_zero(roots.device_secret + FV_DEVICE_ROOT_SIZE,
                 FV_DEVICE_ROOT_SIZE) ||
        all_zero(created.vault_id, sizeof(created.vault_id))) {
        goto cleanup;
    }
    if (!fv_dual_journal_authenticator_init(
            &authenticator, &roots, provisioning->device_context)) {
        result = FV_PROVISION_JOURNAL_FAILED;
        goto cleanup;
    }
    authenticator_ready = true;
    if (!fv_security_journal_init(&journal, provisioning->journal_flash,
                                  &authenticator.interface) ||
        !erase_journal(provisioning->journal_flash) ||
        fv_security_journal_append(&journal, &created) != FV_JOURNAL_OK) {
        result = FV_PROVISION_JOURNAL_FAILED;
        goto cleanup;
    }
    fv_journal_state_t recovered;
    if (fv_security_journal_recover(&journal, &recovered) != FV_JOURNAL_OK ||
        !state_matches(&created, &recovered)) {
        secure_clear(&recovered, sizeof(recovered));
        result = FV_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    secure_clear(&recovered, sizeof(recovered));

    if (fv_device_roots_provision(provisioning->roots_storage, &roots) !=
        FV_DEVICE_ROOTS_OK) {
        result = FV_PROVISION_ROOTS_FAILED;
        goto cleanup;
    }

    /* Verify using roots read back through the storage interface, not RAM. */
    fv_device_secret_t stored_roots;
    if (fv_device_roots_read(provisioning->roots_storage, &stored_roots) !=
            FV_DEVICE_ROOTS_ACTIVE ||
        memcmp(&stored_roots, &roots, sizeof(roots)) != 0) {
        secure_clear(&stored_roots, sizeof(stored_roots));
        result = FV_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    fv_dual_journal_authenticator_deinit(&authenticator);
    authenticator_ready = false;
    if (!fv_dual_journal_authenticator_init(
            &authenticator, &stored_roots, provisioning->device_context)) {
        secure_clear(&stored_roots, sizeof(stored_roots));
        result = FV_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    authenticator_ready = true;
    secure_clear(&stored_roots, sizeof(stored_roots));
    if (!fv_security_journal_init(&journal, provisioning->journal_flash,
                                  &authenticator.interface) ||
        fv_security_journal_recover(&journal, &recovered) != FV_JOURNAL_OK ||
        !state_matches(&created, &recovered)) {
        secure_clear(&recovered, sizeof(recovered));
        result = FV_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    secure_clear(&recovered, sizeof(recovered));
    *initial_state = created;
    result = FV_PROVISION_OK;

cleanup:
    if (authenticator_ready) {
        fv_dual_journal_authenticator_deinit(&authenticator);
    }
    secure_clear(&roots, sizeof(roots));
    if (result != FV_PROVISION_OK) secure_clear(&created, sizeof(created));
    return result;
}
