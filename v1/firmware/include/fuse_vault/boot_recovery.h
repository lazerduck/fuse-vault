#ifndef FUSE_VAULT_BOOT_RECOVERY_H
#define FUSE_VAULT_BOOT_RECOVERY_H

#include "fuse_vault/persistence.h"

typedef enum {
    FV_BOOT_RECOVERY_OK = 0,
    FV_BOOT_RECOVERY_INVALID,
    FV_BOOT_RECOVERY_UNAVAILABLE,
} fv_boot_recovery_result_t;

/*
 * Recovers the entry method for a provisioned vault. The platform load
 * operation is responsible for record-integrity checking; this boundary then
 * validates the decoded header before translating its stable method ID into
 * the UI enum. A provisioned device must not continue unless this succeeds.
 */
fv_boot_recovery_result_t fv_boot_recover_entry_method(
    fv_platform_services_t *services, fv_entry_method_t *entry_method);

#endif
