#ifndef FV_FIDO_STORE_H
#define FV_FIDO_STORE_H
#include "fuse_vault/vault.h"
#include "fuse_vault/fido_engine.h"

#define FV_FIDO_REGION_BASE 16u
#define FV_FIDO_BANK_SECTORS 1024u
#define FV_FIDO_IMAGE_SECTORS (FV_FIDO_STORE_BYTES / FV_BLOCK_SIZE)

typedef enum {
    FV_FIDO_STORE_OK=0, FV_FIDO_STORE_INVALID=-1, FV_FIDO_STORE_LOCKED=-2,
    FV_FIDO_STORE_IO=-3, FV_FIDO_STORE_CORRUPT=-4, FV_FIDO_STORE_STALE=-5
} fv_fido_store_result;
/* Zero initialize. One serialized owner, no aliases/reentrant callbacks. Fields
 * private. No persistent derived secrets. Vault/platform must outlive this object.
 * Close before lock, credential changes, media replacement or another store owner.
 * Every operation checks the unlocked vault, active internal authority and
 * original credential generation.
 * These checks are not a substitute for firmware session invalidation (F3). */
typedef struct {
    const fv_vault *vault;
    uint8_t descriptor[FV_VOLUME_DESCRIPTOR_BYTES], digest[32];
    uint64_t credential_generation, generation;
    unsigned bank;
    bool ready;
} fv_fido_store;

/* Read-only; fails on absent/corrupt snapshots and wipes output on error. Only
 * call with a successfully unlocked vault. No automatic format/fresh-store path. */
fv_fido_store_result fv_fido_store_open(fv_fido_store *, const fv_vault *,
    uint8_t image[FV_FIDO_STORE_BYTES]);
/* DESTRUCTIVE: trusted local setup only, after explicit user confirmation.
 * Replaces both banks with one committed all-FF initial engine image. Cannot
 * authenticate user intent itself; false confirmed performs no writes.
 * Never call as automatic recovery from open failure. */
fv_fido_store_result fv_fido_store_initialize(fv_fido_store *, const fv_vault *,
    bool confirmed, uint8_t image[FV_FIDO_STORE_BYTES]);
/* Input is unchanged. Success means data/tags/manifest synchronized and verified.
 * Any failure faults the store; close/reopen before retry. An uncertain failed
 * commit can have persisted: reopening may recover the old OR new complete image. */
fv_fido_store_result fv_fido_store_commit(fv_fido_store *,
    const uint8_t image[FV_FIDO_STORE_BYTES]);
/* Engine wrapping key, separately derived from bank and USB keys. Wiped on error.
 * Caller must wipe it after engine_open and close the engine when vault locks. */
fv_fido_store_result fv_fido_store_engine_key(const fv_fido_store *, uint8_t key[32]);
void fv_fido_store_close(fv_fido_store *);
#endif
