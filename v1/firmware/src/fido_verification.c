#include "fuse_vault/fido_verification.h"
#include <string.h>
void fv_fido_verification_clear(fv_fido_verification_t *s) {
    if (s) memset(s, 0, sizeof(*s));
}
void fv_fido_verification_begin(fv_fido_verification_t *s, uint32_t now) {
    fv_fido_verification_clear(s);
    s->verified_at = now; s->valid = true;
}
bool fv_fido_verification_use(fv_fido_verification_t *s, uint32_t now,
                               const uint8_t *rp_hash) {
    if (!s || !s->valid) return false;
    uint32_t age = (uint32_t)(now - s->verified_at);
    if (age >= FV_FIDO_UV_COMPLETE_MS || (!s->started && age >= FV_FIDO_UV_START_MS) ||
        (rp_hash && s->bound && memcmp(rp_hash, s->rp_hash, 32) != 0)) {
        fv_fido_verification_clear(s); return false;
    }
    s->started = true;
    if (rp_hash && !s->bound) { memcpy(s->rp_hash, rp_hash, 32); s->bound = true; }
    return true;
}
