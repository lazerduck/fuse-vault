#ifndef FUSE_VAULT_ENCRYPTED_BLOCK_H
#define FUSE_VAULT_ENCRYPTED_BLOCK_H

#include "fuse_vault/block_device.h"
#include "fuse_vault/credential_envelope.h"

#define FV_ENCRYPTED_BLOCK_FORMAT_VERSION 1u
#define FV_ENCRYPTED_BLOCK_PHYSICAL_BLOCKS_PER_LOGICAL 4u
#define FV_ENCRYPTED_BLOCK_EPOCH_SIZE 16u

typedef bool (*fv_encrypted_block_random_fill_fn)(void *context,
                                                   uint8_t *output,
                                                   size_t length);

/* Public so ownership and erasure can be audited. Do not copy an active
 * session. Each init obtains a fresh random epoch; an epoch must never be
 * deliberately replayed, including after a reset or media rollback. */
typedef struct {
    fv_block_device_t interface;
    fv_block_device_t *untrusted;
    uint8_t encryption_key[16];
    uint8_t nonce_key[32];
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    uint8_t epoch[FV_ENCRYPTED_BLOCK_EPOCH_SIZE];
    uint64_t next_counter;
    uint64_t logical_blocks;
    bool ready;
} fv_encrypted_block_t;

bool fv_encrypted_block_init(fv_encrypted_block_t *encrypted,
                             fv_block_device_t *untrusted,
                             const fv_volume_master_key_t *vmk,
                             const uint8_t vault_id[FV_VAULT_ID_SIZE],
                             fv_encrypted_block_random_fill_fn random_fill,
                             void *random_context);
void fv_encrypted_block_lock(fv_encrypted_block_t *encrypted);
void fv_encrypted_block_fault(fv_encrypted_block_t *encrypted);

#endif
