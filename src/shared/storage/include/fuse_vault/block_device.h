#ifndef FUSE_VAULT_BLOCK_DEVICE_H
#define FUSE_VAULT_BLOCK_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_BLOCK_SIZE 512u

typedef enum {
    FV_BLOCK_OK = 0,
    FV_BLOCK_ERROR_INVALID_ARGUMENT,
    FV_BLOCK_ERROR_NOT_READY,
    FV_BLOCK_ERROR_OUT_OF_RANGE,
    FV_BLOCK_ERROR_IO,
    FV_BLOCK_ERROR_READ_ONLY,
    FV_BLOCK_ERROR_INTEGRITY,
} fv_block_result_t;

typedef struct fv_block_device fv_block_device_t;

typedef struct {
    fv_block_result_t (*read)(fv_block_device_t *device,
                              uint64_t first_block,
                              uint32_t block_count,
                              uint8_t *output);
    fv_block_result_t (*write)(fv_block_device_t *device,
                               uint64_t first_block,
                               uint32_t block_count,
                               const uint8_t *input);
    fv_block_result_t (*sync)(fv_block_device_t *device);
    uint64_t (*block_count)(const fv_block_device_t *device);
    bool (*is_present)(const fv_block_device_t *device);
} fv_block_device_ops_t;

struct fv_block_device {
    const fv_block_device_ops_t *ops;
    void *context;
};

#endif
