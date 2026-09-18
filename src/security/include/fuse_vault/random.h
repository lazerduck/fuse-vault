#ifndef FV_RANDOM_H
#define FV_RANDOM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <mbedtls/ctr_drbg.h>
/* Supplies one HEALTH-CHECKED 192-bit entropy block, or fails. No raw/public
 * fallback. Source configuration/entropy assessment are platform responsibilities. */
typedef int (*fv_entropy_block)(void *,uint8_t out[24]);
typedef struct {
    mbedtls_ctr_drbg_context drbg;
    fv_entropy_block source;
    void *source_context;
    uint8_t previous[24];
    uint32_t blocks;
    bool previous_valid,ready,failed;
} fv_random;
/* Zero-initialize; single owner, no other consumers of the underlying TRNG. */
int fv_random_init(fv_random *,fv_entropy_block,void *context);
int fv_random_generate(void *,uint8_t *,size_t);
void fv_random_clear(fv_random *);
#endif
