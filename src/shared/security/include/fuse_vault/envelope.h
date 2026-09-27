#ifndef FV_ENVELOPE_H
#define FV_ENVELOPE_H
#include "fuse_vault/security.h"
#define FV_ENVELOPE_CONSTRUCTION 0x0101u
/* Four fixed slots, active prefix, all unused slots zero. Review-stage format. */
typedef struct {
    fv_volume_descriptor volume;
    uint8_t device_id[16];uint64_t credential_generation;
    uint16_t credential_profile;uint32_t iterations;uint32_t token_slot;
    fv_auth_policy policy;
} fv_envelope_config;
typedef struct {uint32_t minimum,maximum;} fv_kdf_limits;
typedef int (*fv_random_bytes)(void *context,uint8_t *out,size_t bytes);
/* Cost limits are trusted platform policy. No default production iteration count. */
bool fv_envelope_parse(const uint8_t *,size_t,uint64_t capacity,fv_kdf_limits,fv_envelope_config *);
/* Low-level primitives for the authority adapter/session only; not host commands.
 * Input/output spans must be disjoint. A binding key requires both root and token. */
int fv_vault_binding(const uint8_t root[32],const uint8_t token[32],uint32_t slot,
    const uint8_t volume_id[16],uint8_t binding[32]);
int fv_envelope_seal(const fv_envelope_config *,uint64_t capacity,fv_kdf_limits,
    const uint8_t binding[32],const uint8_t *secret,size_t secret_bytes,const uint8_t vmk[32],
    fv_random_bytes,void *rng_context,uint8_t header[512]);
int fv_envelope_open(const uint8_t header[512],uint64_t capacity,fv_kdf_limits,
    const uint8_t binding[32],const uint8_t *secret,size_t secret_bytes,uint8_t vmk[32]);
#endif
