#include "fuse_vault/envelope.h"
#include "fuse_vault/hmac.h"
#include "key_wrap.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool allzero(const uint8_t *p,size_t n){unsigned x=0;for(size_t i=0;i<n;i++)x|=p[i];return !x;}
static bool cost(uint32_t iterations,fv_kdf_limits l){return l.minimum && l.maximum>=l.minimum && iterations>=l.minimum && iterations<=l.maximum;}
static bool profile_secret(uint16_t p,const uint8_t *secret,size_t n) {
    if(!secret || !n || n>256 || (p<1 || p>4))return false;
    if(p==2)for(size_t i=0;i<n;i++)if(secret[i]<1 || secret[i]>5)return false;
    if(p==3 || p==4){
        if(n!=4)return false;
        for(size_t i=0;i<n;i++)if(secret[i]>=(p==3?100:64))return false;
    }
    /* Profile 1 accepts exact UTF-8 bytes from a validating text UI; no normalization here. */
    return true;
}
static bool encode(const fv_envelope_config *c,uint64_t capacity,fv_kdf_limits limits,uint8_t h[512]) {
    memset(h,0,512);
    if(!c || !cost(c->iterations,limits) || !c->credential_generation ||
       (c->credential_profile<1 || c->credential_profile>4) || allzero(c->device_id,16) ||
       !fv_volume_descriptor_encode(&c->volume,capacity,h) || !fv_auth_policy_encode(&c->policy,h+208))return false;
    memcpy(h+128,c->device_id,16);put(h+144,c->credential_generation,8);
    put(h+152,c->credential_profile,2);put(h+154,1,2);put(h+156,c->iterations,4);
    put(h+192,FV_ENVELOPE_CONSTRUCTION,2);h[194]=c->volume.layer_count;
    put(h+196,40u*c->volume.layer_count,2);put(h+200,c->token_slot,4);
    for(unsigned i=0;i<c->volume.layer_count;i++)put(h+224+2*i,c->volume.cipher_ids[i],2);
    return true;
}
bool fv_envelope_parse(const uint8_t *h,size_t bytes,uint64_t capacity,fv_kdf_limits limits,fv_envelope_config *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));if(!h || bytes!=512)return false;
    fv_envelope_config c={0};uint8_t canonical[512];
    if(!fv_volume_descriptor_decode(h,128,capacity,&c.volume) || !fv_auth_policy_decode(h+208,16,&c.policy))return false;
    memcpy(c.device_id,h+128,16);c.credential_generation=get(h+144,8);
    c.credential_profile=(uint16_t)get(h+152,2);c.iterations=(uint32_t)get(h+156,4);c.token_slot=(uint32_t)get(h+200,4);
    if(!encode(&c,capacity,limits,canonical))return false;
    memcpy(canonical+160,h+160,32);
    memcpy(canonical+256,h+256,40u*c.volume.layer_count);memcpy(canonical+416,h+416,32);
    if(memcmp(canonical,h,512))return false;
    *out=c;return true;
}
int fv_vault_binding(const uint8_t root[32],const uint8_t token[32],uint32_t slot,const uint8_t id[16],uint8_t out[32]) {
    if(!out)return -1;
    memset(out,0,32);fv_hmac h={0};int r=-1;
    static const char label[]="FV2/vault-binding/v1";uint8_t ctx[sizeof(label)+4+16];
    if(!root || !token || !id)goto done;
    memcpy(ctx,label,sizeof(label));put(ctx+sizeof(label),slot,4);memcpy(ctx+sizeof(label)+4,id,16);
    if(!fv_hmac_init(&h,root,32))r=fv_hmac_compute(&h,ctx,sizeof(ctx),token,32,out);
 done:fv_hmac_clear(&h);if(r)mbedtls_platform_zeroize(out,32);return r;
}
static int derive(const uint8_t h[512],const uint8_t binding[32],const uint8_t *secret,size_t bytes,
                  uint8_t keys[4][32],uint8_t mac[32]) {
    uint8_t context[160],prefix[220],bound[32],password[32],prk[32];fv_hmac hm={0};int r=-1;
    static const char unlock[]="FV2/unlock-input/v2",saltlabel[]="FV2/password-salt/v1";
    static const char layer[]="FV2/share-wrap/v1",tag[]="FV2/envelope-mac/v1";
    if(!binding || !profile_secret((uint16_t)get(h+152,2),secret,bytes))goto done;
    if(mbedtls_sha256(h,128,context,0))goto done;
    memcpy(context+32,h+128,128);
    memcpy(prefix,unlock,sizeof(unlock));memcpy(prefix+sizeof(unlock),context,160);
    put(prefix+sizeof(unlock)+160,bytes,4);
    if(fv_hmac_init(&hm,binding,32) || fv_hmac_compute(&hm,prefix,sizeof(unlock)+164,secret,bytes,bound))goto done;
    memcpy(prefix,saltlabel,sizeof(saltlabel));memcpy(prefix+sizeof(saltlabel),h+160,32);
    if(fv_pbkdf2_sha256_32(bound,32,prefix,sizeof(saltlabel)+32,(uint32_t)get(h+156,4),password) ||
       fv_hkdf_extract(h+160,32,password,32,prk))goto done;
    for(unsigned i=0;i<h[194];i++) {
        size_t n=sizeof(layer);memcpy(prefix,layer,n);memcpy(prefix+n,context,160);n+=160;
        put(prefix+n,i,4);n+=4;memcpy(prefix+n,h+224+2*i,2);n+=2;
        if(fv_hkdf_expand(prk,prefix,n,keys[i],32))goto done;
    }
    memcpy(prefix,tag,sizeof(tag));memcpy(prefix+sizeof(tag),context,160);
    if(fv_hkdf_expand(prk,prefix,sizeof(tag)+160,mac,32))goto done;
    r=0;
 done:
    fv_hmac_clear(&hm);mbedtls_platform_zeroize(bound,32);mbedtls_platform_zeroize(password,32);mbedtls_platform_zeroize(prk,32);
    mbedtls_platform_zeroize(prefix,sizeof(prefix));if(r){mbedtls_platform_zeroize(keys,128);mbedtls_platform_zeroize(mac,32);}return r;
}
static int authenticate(const uint8_t h[512],const uint8_t key[32],uint8_t out[32]) {
    static const char label[]="FV2/envelope/v1";uint8_t bytes[480];fv_hmac hm={0};int r=-1;
    memcpy(bytes,h,416);memcpy(bytes+416,h+448,64);
    if(!fv_hmac_init(&hm,key,32))r=fv_hmac_compute(&hm,(const uint8_t*)label,sizeof(label),bytes,480,out);
    fv_hmac_clear(&hm);return r;
}
int fv_envelope_seal(const fv_envelope_config *c,uint64_t capacity,fv_kdf_limits limits,
    const uint8_t binding[32],const uint8_t *secret,size_t n,const uint8_t vmk[32],fv_random_bytes rng,void *ctx,uint8_t h[512]) {
    if(!h)return -1;
    memset(h,0,512);uint8_t keys[4][32]={{0}},mac[32],share[32],last[32];int r=-1;
    if(!vmk || !rng || !encode(c,capacity,limits,h) || !profile_secret(c->credential_profile,secret,n))goto done;
    if(rng(ctx,h+160,32) || derive(h,binding,secret,n,keys,mac))goto done;
    memcpy(last,vmk,32);
    for(unsigned i=0;i<c->volume.layer_count;i++) {
        if(i+1<c->volume.layer_count) {
            if(rng(ctx,share,32))goto done;
            for(unsigned j=0;j<32;j++)last[j]^=share[j];
        } else memcpy(share,last,32);
        if(fv_share_wrap(c->volume.cipher_ids[i],keys[i],share,h+256+40*i))goto done;
    }
    if(authenticate(h,mac,h+416))goto done;
    r=0;
 done:
    mbedtls_platform_zeroize(keys,sizeof(keys));mbedtls_platform_zeroize(mac,32);mbedtls_platform_zeroize(share,32);mbedtls_platform_zeroize(last,32);
    if(r)mbedtls_platform_zeroize(h,512);
    return r;
}
int fv_envelope_open(const uint8_t h[512],uint64_t capacity,fv_kdf_limits limits,
    const uint8_t binding[32],const uint8_t *secret,size_t n,uint8_t vmk[32]) {
    if(!vmk)return -1;
    memset(vmk,0,32);fv_envelope_config c;uint8_t keys[4][32]={{0}},mac[32],tag[32],share[32],candidate[32]={0};int r=-1;
    if(!fv_envelope_parse(h,512,capacity,limits,&c) || derive(h,binding,secret,n,keys,mac) ||
       authenticate(h,mac,tag) || !fv_tag_equal(tag,h+416))goto done;
    for(unsigned i=0;i<c.volume.layer_count;i++) {
        if(fv_share_unwrap(c.volume.cipher_ids[i],keys[i],h+256+40*i,share))goto done;
        for(unsigned j=0;j<32;j++)candidate[j]^=share[j];
    }
    memcpy(vmk,candidate,32);r=0;
 done:
    mbedtls_platform_zeroize(keys,sizeof(keys));mbedtls_platform_zeroize(mac,32);mbedtls_platform_zeroize(tag,32);
    mbedtls_platform_zeroize(share,32);mbedtls_platform_zeroize(candidate,32);return r;
}
