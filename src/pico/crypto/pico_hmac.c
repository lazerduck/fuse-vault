#include "fuse_vault/hmac.h"
#include "pico/sha256.h"
#include <mbedtls/platform_util.h>
#include <string.h>
/* Core 1 owns this operation. Use the SDK lock, but never wait indefinitely for
 * another owner or fall back to software. CPU feeding uses no DMA resources.
 * Pads are replayed because the SDK does not expose hash-state restoration. */
static int hash(const uint8_t pad[64],const uint8_t *p,size_t n,
                const uint8_t *data,size_t bytes,uint8_t out[32]) {
    pico_sha256_state_t state;
    if(pico_sha256_try_start(&state,SHA256_BIG_ENDIAN,false)!=PICO_OK)return -1;
    pico_sha256_update_blocking(&state,pad,64);
    if(n)pico_sha256_update_blocking(&state,p,n);
    if(bytes)pico_sha256_update_blocking(&state,data,bytes);
    if(sha256_err_not_ready()) {
        pico_sha256_finish(&state,NULL); /* Abandon and release ownership. */
        mbedtls_platform_zeroize(&state,sizeof(state));
        return -1;
    }
    sha256_result_t result;
    pico_sha256_finish(&state,&result); /* Padding, final result, unlock. */
    memcpy(out,result.bytes,32);
    mbedtls_platform_zeroize(&result,sizeof(result));
    mbedtls_platform_zeroize(&state,sizeof(state));
    return 0;
}
int fv_hmac_compute_pico(const fv_hmac *h,const uint8_t *p,size_t n,
                         const uint8_t *data,size_t bytes,uint8_t tag[32]) {
    if(!h || !h->ready || (!p&&n) || (!data&&bytes) || !tag)return -1;
    uint8_t inner[32];
    int result=hash(h->ipad,p,n,data,bytes,inner);
    if(!result)result=hash(h->opad,NULL,0,inner,32,tag);
    mbedtls_platform_zeroize(inner,sizeof(inner));
    if(result)mbedtls_platform_zeroize(tag,32);
    return result;
}
