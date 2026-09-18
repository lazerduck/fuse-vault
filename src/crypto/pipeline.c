#include "fuse_vault/crypto.h"
#include <string.h>
#include <mbedtls/platform_util.h>
void fv_pipeline_clear(fv_pipeline *p) { if (p) mbedtls_platform_zeroize(p, sizeof(*p)); }
fv_status fv_pipeline_init(fv_pipeline *p, const fv_algorithm *a,
                           const uint8_t keys[][FV_XTS_KEY_BYTES], size_t n) {
    if (!p) return FV_INVALID;
    fv_pipeline_clear(p);
    if (!a || !keys || !n || n > FV_MAX_LAYERS) return FV_INVALID;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < i; ++j)
            if (!memcmp(keys[i], keys[j], FV_XTS_KEY_BYTES)) goto invalid;
        fv_status s = fv_cipher_init(&p->layers[i], a[i], keys[i], FV_XTS_KEY_BYTES);
        if (s != FV_OK) { fv_pipeline_clear(p); return s; }
    }
    p->count = n;
    return FV_OK;
invalid:
    fv_pipeline_clear(p);
    return FV_INVALID;
}
static fv_status run(fv_pipeline *p, uint64_t l, const uint8_t *in, uint8_t *out, int enc) {
    if (!p || !p->count || p->count > FV_MAX_LAYERS || !in || !out) return FV_INVALID;
    for (size_t i = 0; i < p->count; ++i) {
        size_t index = enc ? i : p->count - 1 - i;
        fv_status s = enc ? fv_cipher_encrypt(&p->layers[index], l, in, out) :
                            fv_cipher_decrypt(&p->layers[index], l, in, out);
        if (s != FV_OK) return s;
        in = out;
    }
    return FV_OK;
}
fv_status fv_pipeline_encrypt(fv_pipeline *p,uint64_t l,const uint8_t *i,uint8_t *o) { return run(p,l,i,o,1); }
fv_status fv_pipeline_decrypt(fv_pipeline *p,uint64_t l,const uint8_t *i,uint8_t *o) { return run(p,l,i,o,0); }
