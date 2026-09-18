#ifndef FV_CRYPTO_INTERNAL_H
#define FV_CRYPTO_INTERNAL_H
#include "fuse_vault/crypto.h"
typedef struct fv_cipher_ops {
    const char *name;
    int (*init)(fv_key_context *, const uint8_t *);
    int (*block)(fv_key_context *, int, int, const uint8_t *, uint8_t *);
} fv_cipher_ops;
extern const fv_cipher_ops fv_aes_ops, fv_camellia_ops;
#endif
