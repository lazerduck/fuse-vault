#ifndef FUSE_VAULT_FIDO_VERIFICATION_H
#define FUSE_VAULT_FIDO_VERIFICATION_H
#include <stdbool.h>
#include <stdint.h>
#define FV_FIDO_UV_START_MS 30000u
#define FV_FIDO_UV_COMPLETE_MS 600000u
typedef enum {FV_FIDO_UV_STRICT=0,FV_FIDO_UV_SESSION=1} fv_fido_uv_policy;
typedef struct {
    uint32_t verified_at;
    uint8_t rp_hash[32];
    bool valid, started, bound;
} fv_fido_verification_t;
bool fv_fido_verification_use_policy(fv_fido_verification_t *,uint32_t,const uint8_t *,fv_fido_uv_policy);
void fv_fido_verification_begin(fv_fido_verification_t *state, uint32_t now);
void fv_fido_verification_clear(fv_fido_verification_t *state);
/* NULL RP is for management operations; later first RP use binds the cache. */
bool fv_fido_verification_use(fv_fido_verification_t *state, uint32_t now,
                               const uint8_t *rp_hash);
#endif
