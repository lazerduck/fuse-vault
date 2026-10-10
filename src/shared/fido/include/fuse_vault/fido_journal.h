#ifndef FV_FIDO_JOURNAL_H
#define FV_FIDO_JOURNAL_H
#include "fuse_vault/fido_store.h"
#define FV_FIDO_DISK_BYTES (824u*1024u)
#define FV_FIDO_JOURNAL_ENTRIES 128u
/* Serialized owner. Close before vault lock, media change or credential change.
 * Open recovers committed transactions and migrates authenticated legacy stores.
 * Uncommitted writes are discarded on close. No plaintext image is retained. */
typedef struct {
    const fv_vault *vault;
    uint8_t descriptor[128];
    uint64_t credential_generation, sequence;
    uint16_t targets[FV_FIDO_JOURNAL_ENTRIES], count;
    fv_pipeline pipeline;
    fv_auth_store auth;
    fv_hmac journal_mac;
    fv_block_device_t overlay;
    alignas(4) uint8_t cache[8][512];
    uint16_t cache_lba[8];
    uint8_t cache_valid;
    bool ready, recovering, creating;
} fv_fido_journal;
int fv_fido_journal_open(fv_fido_journal *, const fv_vault *);
int fv_fido_journal_initialize(fv_fido_journal *, const fv_vault *, bool confirmed);
int fv_fido_journal_prepare_new(fv_fido_journal *, const fv_vault *);
int fv_fido_journal_key(fv_fido_journal *, uint8_t key[32]);
bool fv_fido_journal_read(void *, size_t offset, uint8_t *, size_t);
bool fv_fido_journal_write(void *, size_t offset, const uint8_t *, size_t);
bool fv_fido_journal_commit(void *);
/* Destructive, after trusted UI approval. A durable reset intent precedes writes.
 * keep_policy is -1 for a local reset, 0/1 for host reset's preserved UV policy. */
bool fv_fido_journal_reset(void *, int keep_policy);
void fv_fido_journal_close(fv_fido_journal *);
#endif
