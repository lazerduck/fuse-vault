#include "internal.h"
#include <string.h>
#include <mbedtls/platform_util.h>
void fv_cipher_clear(fv_cipher *c) { if (c) mbedtls_platform_zeroize(c, sizeof(*c)); }
const char *fv_cipher_name(const fv_cipher *c) { return c && c->ops ? c->ops->name : "uninitialized"; }
fv_status fv_cipher_init(fv_cipher *c, fv_algorithm a, const uint8_t *k, size_t n) {
    if (!c) return FV_INVALID;
    fv_cipher_clear(c);
    if (!k || n != FV_XTS_KEY_BYTES || !memcmp(k, k + 32, 32)) return FV_INVALID;
    const fv_cipher_ops *ops = a == FV_AES_256_XTS ? &fv_aes_ops :
                              a == FV_CAMELLIA_256_XTS ? &fv_camellia_ops : NULL;
    if (!ops) return FV_INVALID;
    if (ops->init(&c->key, k)) { fv_cipher_clear(c); return FV_CRYPTO_ERROR; }
    c->ops = ops;
    return FV_OK;
}
/* XEX with the XTS little-endian GF(2^128) multiplication convention.
 * Fixed whole sectors contain 32 complete blocks: no ciphertext stealing. */
static fv_status crypt(fv_cipher *c, uint64_t lba, const uint8_t *in, uint8_t *out, int enc) {
    if (!c || !c->ops || !in || !out) return FV_INVALID;
    uintptr_t x = (uintptr_t)in, y = (uintptr_t)out;
    if (x != y && (x > y ? x - y : y - x) < FV_SECTOR_BYTES) return FV_INVALID;
    uint8_t tweak[16] = {0}, block[16];
    for (unsigned i = 0; i < 8; ++i) tweak[i] = (uint8_t)(lba >> (8 * i));
    fv_status result = FV_CRYPTO_ERROR;
    if (c->ops->block(&c->key, 1, 1, tweak, tweak)) goto done;
    for (size_t offset = 0; offset < FV_SECTOR_BYTES; offset += 16) {
        for (unsigned i = 0; i < 16; ++i) block[i] = in[offset + i] ^ tweak[i];
        if (c->ops->block(&c->key, enc, 0, block, block)) goto done;
        for (unsigned i = 0; i < 16; ++i) out[offset + i] = block[i] ^ tweak[i];
        unsigned carry = 0;
        for (unsigned i = 0; i < 16; ++i) {
            unsigned next = tweak[i] >> 7;
            tweak[i] = (uint8_t)((tweak[i] << 1) | carry);
            carry = next;
        }
        tweak[0] ^= (uint8_t)(0x87u & (0u - carry));
    }
    result = FV_OK;
done:
    mbedtls_platform_zeroize(tweak, sizeof(tweak));
    mbedtls_platform_zeroize(block, sizeof(block));
    return result;
}
fv_status fv_cipher_encrypt(fv_cipher *c, uint64_t l, const uint8_t *i, uint8_t *o) { return crypt(c,l,i,o,1); }
fv_status fv_cipher_decrypt(fv_cipher *c, uint64_t l, const uint8_t *i, uint8_t *o) { return crypt(c,l,i,o,0); }
