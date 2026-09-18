#include "fuse_vault/random.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static int entropy(void *context,unsigned char *out,size_t n) {
    fv_random *r=context;size_t remaining=n;uint8_t block[24];int result=-1;
    while(remaining) {
        if(r->failed || r->source(r->source_context,block))goto done;
        unsigned any=0,not_ff=0;
        for(unsigned i=0;i<24;i++){any|=block[i];not_ff|=block[i]^255;}
        if(!any || !not_ff || (r->previous_valid && !memcmp(r->previous,block,24)))goto done;
        memcpy(r->previous,block,24);r->previous_valid=true;r->blocks++;
        size_t take=remaining<24?remaining:24;
        memcpy(out+n-remaining,block,take);remaining-=take;
    }
    result=0;
done:
    mbedtls_platform_zeroize(block,sizeof(block));
    if(result){r->failed=true;mbedtls_platform_zeroize(out,n);}
    return result;
}
void fv_random_clear(fv_random *r) {
    if(!r)return;
    mbedtls_ctr_drbg_free(&r->drbg);mbedtls_platform_zeroize(r,sizeof(*r));
}
int fv_random_init(fv_random *r,fv_entropy_block source,void *context) {
    if(!r)return -1;
    fv_random_clear(r);mbedtls_ctr_drbg_init(&r->drbg);
    r->source=source;r->source_context=context;
    static const unsigned char domain[]="FV2/RP2354/CTR-DRBG/v1";
    /* 48 bytes = AES-256 seed entropy + nonce material. Library's derivation
     * function remains enabled. Reseed from fresh entropy for EVERY request. */
    mbedtls_ctr_drbg_set_entropy_len(&r->drbg,48);
    if(!source || mbedtls_ctr_drbg_set_nonce_len(&r->drbg,0) ||
       mbedtls_ctr_drbg_seed(&r->drbg,entropy,r,domain,sizeof(domain))) {
        mbedtls_ctr_drbg_free(&r->drbg);
        mbedtls_platform_zeroize(r->previous,24);r->previous_valid=false;
        r->failed=true;return -1;
    }
    mbedtls_ctr_drbg_set_prediction_resistance(&r->drbg,MBEDTLS_CTR_DRBG_PR_ON);
    r->ready=true;return 0;
}
int fv_random_generate(void *context,uint8_t *out,size_t n) {
    fv_random *r=context;
    if(!out || !n || n>MBEDTLS_CTR_DRBG_MAX_REQUEST)return -1;
    memset(out,0,n);
    if(!r || !r->ready || r->failed)return -1;
    if(mbedtls_ctr_drbg_random(&r->drbg,out,n)) {
        mbedtls_platform_zeroize(out,n);mbedtls_ctr_drbg_free(&r->drbg);
        mbedtls_platform_zeroize(r->previous,24);r->previous_valid=false;
        r->ready=false;r->failed=true;return -1;
    }
    return 0;
}
