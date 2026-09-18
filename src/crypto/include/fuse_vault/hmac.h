#ifndef FV_HMAC_H
#define FV_HMAC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <mbedtls/sha256.h>
/* Prepared HMAC-SHA-256 ipad/opad states; no allocation or key setup per sector. */
typedef struct { mbedtls_sha256_context inner, outer; uint8_t ipad[64],opad[64]; bool ready; } fv_hmac;
int fv_hmac_init(fv_hmac *,const uint8_t *key,size_t bytes);
int fv_hmac_compute(const fv_hmac *,const uint8_t *prefix,size_t prefix_bytes,
                    const uint8_t *data,size_t data_bytes,uint8_t tag[32]);
/* Reference is retained for on-target backend comparison, never a silent fallback. */
int fv_hmac_compute_software(const fv_hmac *,const uint8_t *,size_t,const uint8_t *,size_t,uint8_t[32]);
unsigned fv_hmac_backend(void); /* 0 software, 1 Pico CPU-fed SHA accelerator */
bool fv_hmac_self_test(void);
bool fv_tag_equal(const uint8_t a[32],const uint8_t b[32]);
void fv_hmac_clear(fv_hmac *);
#endif
