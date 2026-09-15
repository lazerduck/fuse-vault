#ifndef FUSE_VAULT_FILE_BLOCK_DEVICE_H
#define FUSE_VAULT_FILE_BLOCK_DEVICE_H

#include "fuse_vault/block_device.h"

#define FV_HOST_BLOCK_PATH_CAPACITY 4096u

typedef struct {
    char path[FV_HOST_BLOCK_PATH_CAPACITY];
    uint64_t blocks;
} fv_host_file_block_context_t;

bool fv_host_file_block_device_init(fv_block_device_t *device,
                                    fv_host_file_block_context_t *context,
                                    const char *path, uint64_t blocks);
#endif
