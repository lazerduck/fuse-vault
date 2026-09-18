#include "fuse_vault/security.h"
#include "fuse_vault/hmac.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static int init_key(fv_hmac *h,const uint8_t *key,size_t n) {
    /* HMAC with an empty key is equivalent to a zero-padded all-zero key. */
    static const uint8_t zero[32]={0};
    if(!key && n)return -1;
    return fv_hmac_init(h,n?key:zero,n?n:32);
}
int fv_hkdf_extract(const uint8_t *salt,size_t sn,const uint8_t *ikm,size_t n,uint8_t prk[32]) {
    if(!prk)return -1;
    memset(prk,0,32);fv_hmac h={0};int r=-1;
    if((!ikm&&n) || init_key(&h,salt,sn))goto done;
    r=fv_hmac_compute(&h,NULL,0,ikm,n,prk);
 done:
    fv_hmac_clear(&h);if(r)mbedtls_platform_zeroize(prk,32);return r;
}
int fv_hkdf_expand(const uint8_t prk[32],const uint8_t *info,size_t n,uint8_t *out,size_t bytes) {
    if(!out || !bytes || bytes>255u*32u)return -1;
    memset(out,0,bytes);fv_hmac h={0};uint8_t t[32]={0},message[289];int r=-1;
    if(!prk || (!info&&n) || n>256 || fv_hmac_init(&h,prk,32))goto done;
    size_t written=0,previous=0;unsigned counter=1;
    while(written<bytes) {
        memcpy(message,t,previous);if(n)memcpy(message+previous,info,n);
        message[previous+n]=(uint8_t)counter++;
        if(fv_hmac_compute(&h,NULL,0,message,previous+n+1,t))goto done;
        size_t take=bytes-written;if(take>32)take=32;
        memcpy(out+written,t,take);written+=take;previous=32;
    }
    r=0;
 done:
    fv_hmac_clear(&h);mbedtls_platform_zeroize(t,sizeof(t));mbedtls_platform_zeroize(message,sizeof(message));
    if(r)mbedtls_platform_zeroize(out,bytes);
    return r;
}
int fv_pbkdf2_sha256_32(const uint8_t *password,size_t pn,const uint8_t *salt,size_t sn,uint32_t iterations,uint8_t out[32]) {
    if(!out)return -1;
    memset(out,0,32);fv_hmac h={0};uint8_t u[32]={0},next[32]={0};int r=-1;
    const uint8_t block[4]={0,0,0,1}; /* PBKDF2 block number is big-endian. */
    if(!iterations || (!salt&&sn) || init_key(&h,password,pn))goto done;
    if(fv_hmac_compute(&h,salt,sn,block,4,u))goto done;
    memcpy(out,u,32);
    for(uint32_t i=1;i<iterations;i++) {
        if(fv_hmac_compute(&h,NULL,0,u,32,next))goto done;
        memcpy(u,next,32);for(unsigned j=0;j<32;j++)out[j]^=u[j];
    }
    r=0;
 done:
    fv_hmac_clear(&h);mbedtls_platform_zeroize(u,32);mbedtls_platform_zeroize(next,32);
    if(r)mbedtls_platform_zeroize(out,32);
    return r;
}
void fv_working_keys_clear(fv_working_keys *k) {if(k)mbedtls_platform_zeroize(k,sizeof(*k));}
int fv_derive_working_keys(const uint8_t vmk[32],const fv_volume_descriptor *d,fv_working_keys *out) {
    if(!out)return -1;
    fv_working_keys_clear(out);uint8_t descriptor[128],digest[32],prk[32],info[96];int r=-1;
    static const char layer[]="FV2/xts-layer/v1",integrity[]="FV2/sector-hmac/v1";
    if(!vmk || !fv_volume_descriptor_encode(d,UINT64_MAX,descriptor) ||
       mbedtls_sha256(descriptor,128,digest,0) || fv_hkdf_extract(d->volume_id,16,vmk,32,prk))goto done;
    for(unsigned i=0;i<d->layer_count;i++) {
        size_t n=sizeof(layer);memcpy(info,layer,n);memcpy(info+n,digest,32);n+=32;
        for(unsigned j=0;j<4;j++)info[n++]=(uint8_t)(i>>(8*j));
        info[n++]=(uint8_t)d->cipher_ids[i];info[n++]=(uint8_t)(d->cipher_ids[i]>>8);
        if(fv_hkdf_expand(prk,info,n,out->layers[i],64))goto done;
    }
    memcpy(info,integrity,sizeof(integrity));memcpy(info+sizeof(integrity),digest,32);
    if(fv_hkdf_expand(prk,info,sizeof(integrity)+32,out->integrity,32))goto done;
    r=0;
 done:
    mbedtls_platform_zeroize(prk,32);mbedtls_platform_zeroize(info,sizeof(info));
    if(r)fv_working_keys_clear(out);
    return r;
}
