#ifndef FUSE_VAULT_DEVICE_PROVISIONING_H
#define FUSE_VAULT_DEVICE_PROVISIONING_H

#include "fuse_vault/device_roots.h"
#include "fuse_vault/security_journal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*fv_provision_random_fill_fn)(void *context, uint8_t *output,
                                             size_t length);

typedef enum {
    FV_PROVISION_OK = 0,
    FV_PROVISION_INVALID_ARGUMENT,
    FV_PROVISION_NOT_EMPTY,
    FV_PROVISION_RANDOM_FAILED,
    FV_PROVISION_JOURNAL_FAILED,
    FV_PROVISION_ROOTS_FAILED,
    FV_PROVISION_VERIFICATION_FAILED,
} fv_provision_result_t;

typedef struct {
    fv_device_roots_storage_t *roots_storage;
    fv_journal_flash_t *journal_flash;
    const uint8_t *device_context;
    size_t device_context_length;
    fv_provision_random_fill_fn random_fill;
    void *random_context;
} fv_device_provisioning_t;

/*
 * Creates roots and the first authenticated journal record. The journal is
 * committed and verified before the irreversible OTP active marker is set.
 */
fv_provision_result_t fv_device_provision(
    const fv_device_provisioning_t *provisioning,
    fv_journal_state_t *initial_state);

#endif
