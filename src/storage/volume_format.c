#include "fuse_vault/volume_format.h"
#include <string.h>
static void put(uint8_t *p,uint64_t v,unsigned n) {for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n) {uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool valid(const fv_volume_descriptor *d,uint64_t capacity) {
    if(!d || !d->logical_blocks || d->layer_count<1 || d->layer_count>FV_MAX_LAYERS)return false;
    unsigned nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=d->volume_id[i];
    if(!nonzero)return false;
    for(unsigned i=0;i<FV_MAX_LAYERS;i++) {
        if(i<d->layer_count) {if(d->cipher_ids[i]!=FV_AES_256_XTS && d->cipher_ids[i]!=FV_CAMELLIA_256_XTS)return false;}
        else if(d->cipher_ids[i])return false;
    }
    uint64_t n=d->logical_blocks,m=n/15+(n%15!=0);
    return capacity>=FV_VOLUME_METADATA_BASE && m<=capacity-FV_VOLUME_METADATA_BASE &&
        n<=capacity-FV_VOLUME_METADATA_BASE-m;
}
bool fv_volume_descriptor_encode(const fv_volume_descriptor *d,uint64_t capacity,uint8_t out[128]) {
    if(!out)return false;
    memset(out,0,128);
    if(!valid(d,capacity))return false;
    memcpy(out,"FV2VOL01",8);put(out+8,1,2);put(out+10,512,2);
    memcpy(out+16,d->volume_id,16);put(out+32,512,4);put(out+36,1,2);put(out+38,1,2);
    uint64_t m=d->logical_blocks/15+(d->logical_blocks%15!=0);
    put(out+40,d->logical_blocks,8);put(out+48,FV_VOLUME_METADATA_BASE,8);put(out+56,m,8);
    put(out+64,FV_VOLUME_METADATA_BASE+m,8);put(out+72,16,8);put(out+80,2048,8);
    out[88]=d->layer_count;out[89]=1;
    for(unsigned i=0;i<FV_MAX_LAYERS;i++)put(out+92+2*i,d->cipher_ids[i],2);
    return true;
}
bool fv_volume_descriptor_decode(const uint8_t *in,size_t bytes,uint64_t capacity,fv_volume_descriptor *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!in || bytes!=128)return false;
    fv_volume_descriptor d={0};uint8_t canonical[128];
    memcpy(d.volume_id,in+16,16);d.logical_blocks=get(in+40,8);d.layer_count=in[88];
    for(unsigned i=0;i<FV_MAX_LAYERS;i++)d.cipher_ids[i]=(uint16_t)get(in+92+2*i,2);
    /* Canonical re-encoding checks all constants, redundant layout fields and
     * reserved bytes, not just fields copied into the logical object. */
    if(!fv_volume_descriptor_encode(&d,capacity,canonical) || memcmp(in,canonical,128))return false;
    *out=d;return true;
}
