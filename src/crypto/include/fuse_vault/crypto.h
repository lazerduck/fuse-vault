#ifndef FV_CRYPTO_H
#define FV_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
#include <mbedtls/aes.h>
#include <mbedtls/camellia.h>

#define FV_SECTOR_BYTES 512u
#define FV_XTS_KEY_BYTES 64u
#define FV_MAX_LAYERS 4u

typedef enum { FV_OK = 0, FV_INVALID = -1, FV_CRYPTO_ERROR = -2 } fv_status;
typedef enum { FV_AES_256_XTS = 1, FV_CAMELLIA_256_XTS = 2 } fv_algorithm;

/* Backend storage is concrete for stack/static allocation: no heap, no opaque
 * byte-buffer casts. Treat all fields as private. Do not copy initialized keys. */
typedef union {
    struct { mbedtls_aes_context enc, dec, tweak; } aes;
    struct { mbedtls_camellia_context enc, dec, tweak; } camellia;
} fv_key_context;
struct fv_cipher_ops;
typedef struct { const struct fv_cipher_ops *ops; fv_key_context key; } fv_cipher;
typedef struct { size_t count; fv_cipher layers[FV_MAX_LAYERS]; } fv_pipeline;

/* Objects must be zero-initialized before first use. Init replaces previous
 * state; failure leaves the object cleared. Keys are already-derived bytes:
 * data key first, tweak key second. Equal halves are rejected. */
fv_status fv_cipher_init(fv_cipher *, fv_algorithm, const uint8_t *, size_t);
void fv_cipher_clear(fv_cipher *);
const char *fv_cipher_name(const fv_cipher *);
/* One 512-byte sector. LBA encoded little-endian in the low 64 bits of the
 * 128-bit XTS data-unit input; high bits zero. Same-buffer or disjoint buffers
 * only. On failure output is unusable. XTS does NOT authenticate data. */
fv_status fv_cipher_encrypt(fv_cipher *, uint64_t, const uint8_t *, uint8_t *);
fv_status fv_cipher_decrypt(fv_cipher *, uint64_t, const uint8_t *, uint8_t *);
/* Each layer receives a distinct 64-byte key. Init rejects duplicate complete
 * layer keys; callers must derive independent keys for every purpose. */
fv_status fv_pipeline_init(fv_pipeline *, const fv_algorithm *,
                           const uint8_t keys[][FV_XTS_KEY_BYTES], size_t);
void fv_pipeline_clear(fv_pipeline *);
fv_status fv_pipeline_encrypt(fv_pipeline *, uint64_t, const uint8_t *, uint8_t *);
fv_status fv_pipeline_decrypt(fv_pipeline *, uint64_t, const uint8_t *, uint8_t *);
#endif
