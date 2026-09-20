#ifndef FV_VAULT_H
#define FV_VAULT_H
#include "fuse_vault/envelope.h"
#include "fuse_vault/auth_store.h"

/* Logical device state, NOT an on-flash encoding. Only a trusted adapter may
 * load/commit this. An erased/corrupt journal must never become EMPTY implicitly. */
typedef enum {FV_ENROLLMENT_EMPTY=1,FV_ENROLLMENT_ACTIVE,
    FV_ENROLLMENT_LOCKED,FV_ENROLLMENT_DESTROY_PENDING,FV_ENROLLMENT_DESTROYED} fv_enrollment;
typedef struct {
    fv_enrollment status;
    uint64_t sequence,credential_generation;
    uint8_t device_id[16],volume_id[16],header_hash[32];
    uint32_t token_slot,attempts;
    bool attempt_pending;
    fv_auth_policy policy;
} fv_device_state;
typedef struct {
    void *context;
    int (*load)(void *,fv_device_state *);
    /* Atomic durable replacement: success means durable; error may be uncertain.
     * Compare previous sequence, reject stale writers. Never restore older state. */
    int (*commit)(void *,uint64_t previous_sequence,const fv_device_state *);
    /* Active/pre-provisioned enrollment only; root/token never leave adapter. */
    int (*binding)(void *,const uint8_t volume_id[16],uint32_t token_slot,uint8_t out[32]);
    /* Idempotent, irreversible enrollment invalidation. Success means durable.
     * Missing callback/failure leaves destruction pending and access denied. */
    int (*destroy)(void *,uint32_t token_slot);
} fv_device_authority;
typedef struct {
    fv_block_device_t *sd;
    fv_device_authority authority;
    fv_random_bytes random;
    void *random_context;
    fv_kdf_limits kdf_limits;
    /* Optional initialization workspace; not used by normal reads/writes. */
    uint8_t *format_scratch;
    uint32_t format_sectors;
    fv_format_progress format_progress;
    void *format_context;
} fv_vault_platform;
typedef enum {FV_VAULT_OK=0,FV_VAULT_INVALID=-1,FV_VAULT_STATE=-2,
    FV_VAULT_IO=-3,FV_VAULT_AUTH=-4,FV_VAULT_DENIED=-5,
    FV_VAULT_CHANGED_NEEDS_MIRROR=1} fv_vault_result;
/* Zero initialize; fields private. Single owner, no concurrent/reentrant calls.
 * Platform and session must outlive I/O; buffers must not alias either object. */
typedef struct {
    fv_pipeline pipeline;
    fv_auth_store store;
    fv_envelope_config config;
    const fv_vault_platform *platform;
    uint8_t vmk[32];
    bool unlocked;
} fv_vault;
void fv_vault_lock(fv_vault *);
/* Read-only UI hint from the flash-anchored header; no guess or attempt commit. */
fv_vault_result fv_vault_credential_profile(const fv_vault_platform *,uint16_t *);
/* Call at boot even without an SD. Completes pending failure/limit handling. */
fv_vault_result fv_vault_recover(const fv_vault_platform *);
/* Destructive SD initialization, only with explicitly pre-provisioned EMPTY
 * device state. Generates VMK/volume ID. Returns locked. Algorithms are 1..4
 * active entries; remainder zero. No production RNG is supplied by this module. */
fv_vault_result fv_vault_create(const fv_vault_platform *,uint64_t logical_blocks,
    const uint16_t algorithms[4],uint8_t count,uint16_t profile,uint32_t iterations,
    fv_auth_policy,const uint8_t *secret,size_t secret_bytes);
fv_vault_result fv_vault_unlock(fv_vault *,const fv_vault_platform *,const uint8_t *,size_t);
/* Fresh current-credential verification, under the attempt budget. Same VMK and
 * storage descriptor. New salt/shares/generation. Locks on every exit. Caller is
 * trusted on-device UI; policy changes additionally require physical approval. */
fv_vault_result fv_vault_change_credential(fv_vault *,const fv_vault_platform *,
    const uint8_t *current,size_t current_bytes,const uint8_t *replacement,size_t replacement_bytes,
    uint16_t profile,uint32_t iterations,fv_auth_policy,bool policy_change_approved);
/* After unlocking, restore the other copy of the anchored header. Does not
 * change keys/generation or reset attempts. Failure locks the session. */
fv_vault_result fv_vault_repair_headers(fv_vault *);
/* In-place 1..64 sectors, four-byte aligned. Reads authenticate the WHOLE batch
 * before decrypting. Writes consume/erase plaintext on valid calls, including
 * failure, and sync before success. No atomic multi-sector write guarantee.
 * Invalid count/pointer/alignment rejected without touching buffer. */
fv_block_result_t fv_vault_read(fv_vault *,uint64_t,uint32_t,uint8_t *);
fv_block_result_t fv_vault_write(fv_vault *,uint64_t,uint32_t,uint8_t *);
#endif
