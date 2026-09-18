#include "fuse_vault/hmac.h"
#include <mbedtls/platform_util.h>
#include <string.h>
void fv_hmac_clear(fv_hmac *h) {if(h)mbedtls_platform_zeroize(h,sizeof(*h));}
int fv_hmac_init(fv_hmac *h,const uint8_t *key,size_t bytes) {
    if(!h)return -1;
    fv_hmac_clear(h);
    if(!key || !bytes)return -1;
    uint8_t block[64]={0};int result=-1;
    if(bytes>64) {if(mbedtls_sha256(key,bytes,block,0))goto done;}
    else memcpy(block,key,bytes);
    for(unsigned i=0;i<64;i++)block[i]^=0x36;
    memcpy(h->ipad,block,64);
    if(mbedtls_sha256_starts(&h->inner,0) || mbedtls_sha256_update(&h->inner,block,64))goto done;
    for(unsigned i=0;i<64;i++)block[i]^=0x36^0x5c;
    memcpy(h->opad,block,64);
    if(mbedtls_sha256_starts(&h->outer,0) || mbedtls_sha256_update(&h->outer,block,64))goto done;
    h->ready=true;result=0;
done:
    mbedtls_platform_zeroize(block,sizeof(block));
    if(result)fv_hmac_clear(h);
    return result;
}
int fv_hmac_compute_software(const fv_hmac *h,const uint8_t *prefix,size_t n,
                    const uint8_t *data,size_t bytes,uint8_t tag[32]) {
    if(!h || !h->ready || (!prefix&&n) || (!data&&bytes) || !tag)return -1;
    mbedtls_sha256_context c;uint8_t inner[32];int result=-1;
    mbedtls_sha256_init(&c);mbedtls_sha256_clone(&c,&h->inner);
    if(mbedtls_sha256_update(&c,prefix,n) || mbedtls_sha256_update(&c,data,bytes) ||
       mbedtls_sha256_finish(&c,inner))goto done;
    mbedtls_sha256_clone(&c,&h->outer);
    if(mbedtls_sha256_update(&c,inner,32) || mbedtls_sha256_finish(&c,tag))goto done;
    result=0;
done:
    mbedtls_sha256_free(&c);mbedtls_platform_zeroize(inner,sizeof(inner));return result;
}
bool fv_tag_equal(const uint8_t a[32],const uint8_t b[32]) {
    unsigned difference=0;
    for(unsigned i=0;i<32;i++)difference|=a[i]^b[i];
    return difference==0;
}

#ifdef FV_HMAC_PICO
int fv_hmac_compute_pico(const fv_hmac *,const uint8_t *,size_t,const uint8_t *,size_t,uint8_t[32]);
#endif
unsigned fv_hmac_backend(void) {
#ifdef FV_HMAC_PICO
    return 1;
#else
    return 0;
#endif
}
int fv_hmac_compute(const fv_hmac *h,const uint8_t *p,size_t n,const uint8_t *d,size_t bytes,uint8_t tag[32]) {
#ifdef FV_HMAC_PICO
    return fv_hmac_compute_pico(h,p,n,d,bytes,tag);
#else
    return fv_hmac_compute_software(h,p,n,d,bytes,tag);
#endif
}
