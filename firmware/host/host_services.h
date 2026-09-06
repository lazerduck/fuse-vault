#ifndef FUSE_VAULT_HOST_SERVICES_H
#define FUSE_VAULT_HOST_SERVICES_H

#include "fuse_vault/persistence.h"
#include "fuse_vault/block_device.h"
#include "fuse_vault/security_journal.h"
#include "file_block_device.h"

#include <stdbool.h>

#define FV_HOST_PATH_CAPACITY 4096u

typedef struct {
    char directory[FV_HOST_PATH_CAPACITY];
    fv_block_device_t vault_device;
    fv_host_file_block_context_t vault_device_context;
    char journal_path[FV_HOST_PATH_CAPACITY];
    fv_journal_flash_t journal_flash;
    uint8_t current_vault_id[FV_VAULT_ID_SIZE];
    bool current_vault_id_valid;
    uint64_t fido_blocks;
} fv_host_services_context_t;

bool fv_host_services_init(fv_platform_services_t *services,
                           fv_host_services_context_t *context,
                           const char *directory);

bool fv_host_services_init_sized(fv_platform_services_t *services,
    fv_host_services_context_t *context, const char *directory,
    uint64_t media_blocks, uint64_t fido_blocks);

#endif
