#ifndef FUSE_VAULT_BLOCK_SLICE_H
#define FUSE_VAULT_BLOCK_SLICE_H

#include "fuse_vault/block_device.h"

typedef struct {
    fv_block_device_t interface;
    fv_block_device_t *parent;
    uint64_t first_block;
    uint64_t blocks;
} fv_block_slice_t;

bool fv_block_slice_init(fv_block_slice_t *slice, fv_block_device_t *parent,
                         uint64_t first_block, uint64_t blocks);

#endif
