#include "internal.h"
/* Prepared schedules; SM4 decrypts with the encryption routine and reversed keys. */
static int init(fv_key_context *c, const uint8_t *k) {
    sm4_set_encrypt_key(&c->sm4.enc, k);
    sm4_set_decrypt_key(&c->sm4.dec, k);
    sm4_set_encrypt_key(&c->sm4.tweak, k + 16);
    return 0;
}
static int block(fv_key_context *c, int enc, int tweak, const uint8_t *in, uint8_t *out) {
    sm4_encrypt(tweak ? &c->sm4.tweak : (enc ? &c->sm4.enc : &c->sm4.dec), in, out);
    return 0;
}
const fv_cipher_ops fv_sm4_ops = {"SM4-128-XTS", init, block};
