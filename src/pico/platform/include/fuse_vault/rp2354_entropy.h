#ifndef FV_RP2354_ENTROPY_H
#define FV_RP2354_ENTROPY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t blocks,last_status,autocorrelation,failures,timeouts;
    uint64_t elapsed_us;
    bool failed;
} fv_rp2354_entropy;
/* Core1 owns TRNG. Never link pico_rand/another TRNG consumer into this image. */
void fv_rp2354_entropy_init(fv_rp2354_entropy *);
int fv_rp2354_entropy_block(void *,uint8_t out[24]);
#endif
