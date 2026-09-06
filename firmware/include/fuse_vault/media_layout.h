#ifndef FUSE_VAULT_MEDIA_LAYOUT_H
#define FUSE_VAULT_MEDIA_LAYOUT_H

#include "fuse_vault/block_device.h"
#include "fuse_vault/block_slice.h"
#include "fuse_vault/persistence.h"

#include <stdint.h>

#define FV_MEDIA_FORMAT_VERSION 1u
#define FV_MEDIA_SUPERBLOCK_SLOTS 2u
#define FV_MEDIA_HEADER_START 2u
#define FV_MEDIA_HEADER_BLOCKS 2u
#define FV_MEDIA_DATA_START 32u
#define FV_MEDIA_RECOVERY_BLOCKS 32u
#define FV_MEDIA_DEFAULT_FIDO_BLOCKS 2048u

typedef enum {
    FV_MEDIA_OK = 0,
    FV_MEDIA_ABSENT,
    FV_MEDIA_BLANK,
    FV_MEDIA_FOREIGN,
    FV_MEDIA_UNSUPPORTED,
    FV_MEDIA_INVALID,
    FV_MEDIA_IO_ERROR,
} fv_media_result_t;

typedef struct {
    uint64_t sequence;
    uint64_t physical_blocks;
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    uint64_t header_start;
    uint64_t header_blocks;
    uint64_t data_start;
    uint64_t data_blocks;
    uint64_t fido_start;
    uint64_t fido_blocks;
    uint64_t recovery_start;
    uint64_t recovery_blocks;
} fv_media_layout_t;

/* Classification does not authenticate and releases no geometry. */
fv_media_result_t fv_media_classify(fv_block_device_t *device);

/* Destructive setup primitive. Clears and verifies only the two discovery
 * sectors; authenticated layout creation follows once roots/vault ID exist. */
fv_media_result_t fv_media_prepare_for_initialization(
    fv_block_device_t *device);

fv_media_result_t fv_media_format(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t vault_id[FV_VAULT_ID_SIZE], uint64_t fido_blocks,
    fv_media_layout_t *layout);

fv_media_result_t fv_media_load(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t expected_vault_id[FV_VAULT_ID_SIZE],
    fv_media_layout_t *layout);

bool fv_media_open_header(const fv_media_layout_t *layout,
                          fv_block_device_t *device, fv_block_slice_t *slice);
bool fv_media_open_data(const fv_media_layout_t *layout,
                        fv_block_device_t *device, fv_block_slice_t *slice);
bool fv_media_open_fido(const fv_media_layout_t *layout,
                        fv_block_device_t *device, fv_block_slice_t *slice);

#endif
