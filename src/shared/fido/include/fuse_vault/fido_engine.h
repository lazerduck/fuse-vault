#ifndef FUSE_VAULT_FIDO_ENGINE_H
#define FUSE_VAULT_FIDO_ENGINE_H
#include "fuse_vault/passkeys.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FV_FIDO_STORE_BYTES 131072u
#define FV_FIDO_ENGINE_MESSAGE_SIZE 2048u
#define FV_FIDO_ENGINE_RESPONSE_SIZE 4096u
/* One serialized synchronous instance, owned by the vault worker in firmware.
 * Callbacks must not reenter the engine. Approval/UV callbacks must cooperate
 * with the scheduler. Commit succeeds only after the storage transaction is durable.
 * This library supplies no USB transport, unlock policy, or storage encryption. */
typedef struct {
    bool (*random)(void *, uint8_t *, size_t);
    bool (*commit)(void *, const uint8_t *, size_t);
    int (*presence)(void *); /* 0 approved, 1 timeout, 2 cancelled */
    uint32_t (*millis)(void *);
    /* Required built-in UV using device unlock; no external ClientPIN profile.
     * Platform enforces freshness/RP binding and charges real failed attempts. */
    bool (*verify_user)(void *, const uint8_t *rp_hash);
    uint8_t (*uv_retries)(void *);
    bool (*cancelled)(void *);
    /* Firmware binds reset eligibility to USB enumeration, not engine reopen. */
    bool (*reset_allowed)(void *);
    bool (*local_authorized)(void *); /* Device session only; never host UV. */
    /* Optional bounded storage backend; image is NULL when supplied. */
    size_t storage_bytes;
    void *storage_context;
    bool (*storage_read)(void *, size_t, uint8_t *, size_t);
    bool (*storage_write)(void *, size_t, const uint8_t *, size_t);
    bool (*storage_commit)(void *);
    bool (*storage_reset)(void *, int keep_policy);
    void *context;
} fv_fido_engine_ops_t;
/* With storage callbacks, pass NULL for store; reads/writes are bounded by
 * storage_bytes and share storage_context. Otherwise the legacy test image
 * is caller-owned, writable and exclusively held until close (which wipes
 * it). Caller explicitly supplies a fresh all-FF image or authenticated snapshot.
 * root is a separately derived FIDO wrapping key, NEVER the VMK/OTP root.
 * Failed commit/RNG faults the instance; close and recover before further use. */
bool fv_fido_engine_open(uint8_t store[FV_FIDO_STORE_BYTES],
    const uint8_t root[32], const uint8_t device_id[16],
    const fv_fido_engine_ops_t *ops);
void fv_fido_engine_close(void);
/* Local-only persistent UV reuse policy: 0 strict, 1 unlocked session.
 * Missing setting defaults to strict; unknown encodings fail closed. */
bool fv_fido_engine_uv_policy(bool write,uint8_t *mode);
/* Stateless discovery for locked devices; call only on the engine owner. */
size_t fv_fido_engine_info(bool uv_configured,uint8_t *,size_t);
/* Only called by the local UI after unlock. BEGIN blocks host commands until
 * END/close. DELETE requires the stable ID returned by READ and commits before
 * success. A failure requires closing/recovering the engine. */
bool fv_fido_engine_manage(fv_passkey_action_t action, uint16_t *index,
    uint16_t *count, fv_passkey_t *entry);
/* Response capacity must be at least FV_FIDO_ENGINE_RESPONSE_SIZE, disjoint
 * from request/store. Returns CTAP status followed by CBOR, not HID framing. */
size_t fv_fido_engine_command(const uint8_t *request, size_t size,
                             uint8_t *response, size_t capacity);
size_t fv_fido_engine_command_channel(uint32_t channel, const uint8_t *request,
    size_t size, uint8_t *response, size_t capacity);
#endif
