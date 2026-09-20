#ifndef FUSE_VAULT_FIDO_ENGINE_H
#define FUSE_VAULT_FIDO_ENGINE_H
#include "fuse_vault/passkeys.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FV_FIDO_STORE_BYTES 65536u
#define FV_FIDO_ENGINE_MESSAGE_SIZE 2048u
#define FV_FIDO_ENGINE_RESPONSE_SIZE 4096u
/* One serialized synchronous instance, owned by the vault worker in firmware.
 * Callbacks must not reenter the engine. Approval/UV callbacks must cooperate
 * with the scheduler. Commit succeeds only after the snapshot is durable.
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
    bool (*local_authorized)(void *); /* Device session only; never host UV. */
    void *context;
} fv_fido_engine_ops_t;
/* store is caller-owned, writable and exclusively held until close (which wipes
 * it). Caller explicitly supplies a fresh all-FF image or authenticated snapshot.
 * root is a separately derived FIDO wrapping key, NEVER the VMK/OTP root.
 * Failed commit/RNG faults the instance; close and recover before further use. */
bool fv_fido_engine_open(uint8_t store[FV_FIDO_STORE_BYTES],
    const uint8_t root[32], const uint8_t device_id[16],
    const fv_fido_engine_ops_t *ops);
void fv_fido_engine_close(void);
/* Only called by the local UI after unlock. BEGIN blocks host commands until
 * END/close. DELETE requires the stable ID returned by READ and commits before
 * success. A failure requires closing/recovering the engine. */
bool fv_fido_engine_manage(fv_passkey_action_t action, uint16_t *index,
    uint16_t *count, fv_passkey_t *entry);
size_t fv_fido_engine_command(const uint8_t *request, size_t size,
                             uint8_t *response, size_t capacity);
size_t fv_fido_engine_command_channel(uint32_t channel, const uint8_t *request,
    size_t size, uint8_t *response, size_t capacity);
#endif
