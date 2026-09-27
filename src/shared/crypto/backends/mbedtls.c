#include "internal.h"
/* Prepared encryption, decryption and tweak schedules; no setkey in I/O. */
static int aes_init(fv_key_context *c, const uint8_t *k) {
    mbedtls_aes_init(&c->aes.enc); mbedtls_aes_init(&c->aes.dec);
    mbedtls_aes_init(&c->aes.tweak);
    if (mbedtls_aes_setkey_enc(&c->aes.enc, k, 256) ||
        mbedtls_aes_setkey_dec(&c->aes.dec, k, 256) ||
        mbedtls_aes_setkey_enc(&c->aes.tweak, k + 32, 256)) return -1;
    return 0;
}
static int aes_block(fv_key_context *c, int enc, int tweak,
                     const uint8_t *in, uint8_t *out) {
    return mbedtls_aes_crypt_ecb(tweak ? &c->aes.tweak :
        (enc ? &c->aes.enc : &c->aes.dec), enc ? MBEDTLS_AES_ENCRYPT :
        MBEDTLS_AES_DECRYPT, in, out);
}
static int cam_init(fv_key_context *c, const uint8_t *k) {
    mbedtls_camellia_init(&c->camellia.enc); mbedtls_camellia_init(&c->camellia.dec);
    mbedtls_camellia_init(&c->camellia.tweak);
    if (mbedtls_camellia_setkey_enc(&c->camellia.enc, k, 256) ||
        mbedtls_camellia_setkey_dec(&c->camellia.dec, k, 256) ||
        mbedtls_camellia_setkey_enc(&c->camellia.tweak, k + 32, 256)) return -1;
    return 0;
}
static int cam_block(fv_key_context *c, int enc, int tweak,
                     const uint8_t *in, uint8_t *out) {
    return mbedtls_camellia_crypt_ecb(tweak ? &c->camellia.tweak :
        (enc ? &c->camellia.enc : &c->camellia.dec), enc ? MBEDTLS_CAMELLIA_ENCRYPT :
        MBEDTLS_CAMELLIA_DECRYPT, in, out);
}
const fv_cipher_ops fv_aes_ops = {"AES-256-XTS", aes_init, aes_block};
const fv_cipher_ops fv_camellia_ops = {"Camellia-256-XTS", cam_init, cam_block};
