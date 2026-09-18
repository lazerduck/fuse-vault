#ifndef FV_VOLUME_FORMAT_H
#define FV_VOLUME_FORMAT_H
#include "fuse_vault/crypto.h"
#include <stdbool.h>
/* Review-stage descriptor codec ONLY, not a complete authenticated header.
 * No format freeze or permission to open storage follows from parsing it. */
#define FV_VOLUME_DESCRIPTOR_BYTES 128u
#define FV_VOLUME_HEADER_BYTES 512u
#define FV_VOLUME_METADATA_BASE 2064u

typedef struct {
    uint8_t volume_id[16];
    uint64_t logical_blocks;
    uint8_t layer_count;
    uint16_t cipher_ids[FV_MAX_LAYERS];
} fv_volume_descriptor;
/* Whole-volume capacity is supplied by the trusted block-device adapter.
 * Outputs clear on errors. Input/output buffers must not overlap. */
bool fv_volume_descriptor_encode(const fv_volume_descriptor *, uint64_t capacity,
    uint8_t out[FV_VOLUME_DESCRIPTOR_BYTES]);
bool fv_volume_descriptor_decode(const uint8_t *, size_t bytes, uint64_t capacity,
    fv_volume_descriptor *out);
#endif
