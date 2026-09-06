#ifndef FUSE_VAULT_DEVICE_SERVICES_H
#define FUSE_VAULT_DEVICE_SERVICES_H

#include "fuse_vault/block_device.h"
#include "fuse_vault/device_roots.h"
#include "fuse_vault/persistence.h"
#include "fuse_vault/security_journal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*fv_device_services_random_fill_fn)(void *context,
                                                   uint8_t *output,
                                                   size_t length);

/* Production persistence composition for one physical device. OTP roots and
 * the internal journal remain device-local; headers and encrypted data live on
 * the removable raw-media block device. */
typedef struct {
    fv_device_roots_storage_t *roots_storage;
    fv_journal_flash_t *journal_flash;
    fv_block_device_t *raw_media;
    fv_device_services_random_fill_fn random_fill;
    void *random_context;
    uint8_t device_id[FV_VAULT_ID_SIZE];
    uint8_t current_vault_id[FV_VAULT_ID_SIZE];
    bool current_vault_id_valid;
} fv_device_services_context_t;

bool fv_device_services_init(
    fv_platform_services_t *services, fv_device_services_context_t *context,
    fv_device_roots_storage_t *roots_storage,
    fv_journal_flash_t *journal_flash, fv_block_device_t *raw_media,
    const uint8_t device_id[FV_VAULT_ID_SIZE],
    fv_device_services_random_fill_fn random_fill, void *random_context);

#endif
