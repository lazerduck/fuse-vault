#include "fuse_vault/block_slice.h"

#include <stddef.h>

static bool range(const fv_block_slice_t *slice, uint64_t first,
                  uint32_t count) {
    return count > 0u && first < slice->blocks &&
           (uint64_t)count <= slice->blocks - first;
}
static fv_block_result_t read_blocks(fv_block_device_t *device, uint64_t first,
                                     uint32_t count, uint8_t *output) {
    fv_block_slice_t *slice = device != NULL ? device->context : NULL;
    if (slice == NULL || output == NULL) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if (!range(slice, first, count)) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    return slice->parent->ops->read(slice->parent, slice->first_block + first,
                                    count, output);
}
static fv_block_result_t write_blocks(fv_block_device_t *device, uint64_t first,
                                      uint32_t count, const uint8_t *input) {
    fv_block_slice_t *slice = device != NULL ? device->context : NULL;
    if (slice == NULL || input == NULL) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if (!range(slice, first, count)) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    return slice->parent->ops->write(slice->parent, slice->first_block + first,
                                     count, input);
}
static fv_block_result_t sync_blocks(fv_block_device_t *device) {
    fv_block_slice_t *slice = device != NULL ? device->context : NULL;
    return slice == NULL ? FV_BLOCK_ERROR_INVALID_ARGUMENT
                         : slice->parent->ops->sync(slice->parent);
}
static uint64_t block_count(const fv_block_device_t *device) {
    const fv_block_slice_t *slice = device != NULL ? device->context : NULL;
    return slice != NULL ? slice->blocks : 0u;
}
static bool present(const fv_block_device_t *device) {
    const fv_block_slice_t *slice = device != NULL ? device->context : NULL;
    return slice != NULL && slice->parent->ops->is_present(slice->parent);
}
static const fv_block_device_ops_t OPS = {
    read_blocks, write_blocks, sync_blocks, block_count, present
};

bool fv_block_slice_init(fv_block_slice_t *slice, fv_block_device_t *parent,
                         uint64_t first_block, uint64_t blocks) {
    if (slice == NULL || parent == NULL || parent->ops == NULL || blocks == 0u ||
        parent->ops->block_count == NULL || parent->ops->read == NULL ||
        parent->ops->write == NULL || parent->ops->sync == NULL ||
        parent->ops->is_present == NULL ||
        first_block > parent->ops->block_count(parent) ||
        blocks > parent->ops->block_count(parent) - first_block) return false;
    *slice = (fv_block_slice_t) {
        .interface = {.ops = &OPS, .context = slice},
        .parent = parent, .first_block = first_block, .blocks = blocks,
    };
    return true;
}
