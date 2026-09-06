#ifndef FUSE_VAULT_CRYPTO_PIPELINE_H
#define FUSE_VAULT_CRYPTO_PIPELINE_H

#include "fuse_vault/credential_envelope.h"
#include "fuse_vault/crypto_stack.h"

#include <stdbool.h>
#include <stdint.h>

#define FV_CRYPTO_PIPELINE_KEY_CAPACITY 64u

typedef struct {
    uint16_t algorithm_id;
    uint16_t algorithm_version;
    uint8_t key[FV_CRYPTO_PIPELINE_KEY_CAPACITY];
} fv_crypto_pipeline_layer_t;

typedef struct {
    fv_encryption_stack_descriptor_t descriptor;
    fv_crypto_pipeline_layer_t layers[FV_ENCRYPTION_STACK_MAX_LAYERS];
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    bool ready;
} fv_crypto_pipeline_t;

bool fv_crypto_pipeline_init(
    fv_crypto_pipeline_t *pipeline,
    const fv_encryption_stack_descriptor_t *descriptor,
    const fv_volume_master_key_t *vmk,
    const uint8_t vault_id[FV_VAULT_ID_SIZE]);

bool fv_crypto_pipeline_encrypt_block(
    const fv_crypto_pipeline_t *pipeline, uint64_t logical_block,
    uint64_t generation, const uint8_t epoch[16], uint64_t counter,
    uint8_t block[512]);

bool fv_crypto_pipeline_decrypt_block(
    const fv_crypto_pipeline_t *pipeline, uint64_t logical_block,
    uint64_t generation, const uint8_t epoch[16], uint64_t counter,
    uint8_t block[512]);

void fv_crypto_pipeline_clear(fv_crypto_pipeline_t *pipeline);

#endif
