#ifndef FUSE_VAULT_DEVICE_PROVISIONING_H
#define FUSE_VAULT_DEVICE_PROVISIONING_H

#include "fuse_vault/device_roots.h"
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
    FV_PROVISION_ROOTS_FAILED,
    FV_PROVISION_VERIFICATION_FAILED,
} fv_provision_result_t;

typedef struct {
    fv_device_roots_storage_t *roots_storage;
    fv_provision_random_fill_fn random_fill;
    void *random_context;
} fv_device_provisioning_t;

/*
 * Factory identity operation: creates and verifies device roots only. User
 * setup separately creates the vault ID, SD structures, credential envelope,
 * and first journal record after password/stack selection.
 */
fv_provision_result_t fv_device_provision(
    const fv_device_provisioning_t *provisioning);

#endif
