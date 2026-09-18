#include "fuse_vault/auth_store.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static uint64_t now(fv_auth_store *s){return s->now_us?s->now_us():0;}
void fv_auth_close(fv_auth_store *s){if(s)mbedtls_platform_zeroize(s,sizeof(*s));}
fv_block_result_t fv_auth_open(fv_auth_store *s,fv_block_device_t *d,uint64_t base,
    uint64_t blocks,const uint8_t volume[16],const uint8_t key[32],uint64_t (*timer)(void)) {
    if(!s)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    fv_auth_close(s);
    if(!d || !d->ops || !d->ops->read || !d->ops->write || !d->ops->sync ||
       !d->ops->block_count || !blocks || !volume || !key)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    uint64_t meta=blocks/15+(blocks%15!=0),capacity=d->ops->block_count(d);
    if(base>capacity || meta>capacity-base || blocks>capacity-base-meta)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    if(fv_hmac_init(&s->hmac,key,32))return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    s->device=d;s->base=base;s->blocks=blocks;s->metadata_blocks=meta;s->data_base=base+meta;
    s->now_us=timer;memcpy(s->volume,volume,16);s->ready=true;return FV_BLOCK_OK;
}
static fv_block_result_t fault(fv_auth_store *s,fv_block_result_t r) {
    s->ready=false;s->cache_count=0;return r;
}
static fv_block_result_t meta_io(fv_auth_store *s,uint64_t first,uint32_t count,uint8_t *data,bool write) {
    uint64_t start=now(s);
    fv_block_result_t r=write?s->device->ops->write(s->device,s->base+first,count,data):
                              s->device->ops->read(s->device,s->base+first,count,data);
    s->stats.metadata_us+=now(s)-start;
    if(write)s->stats.metadata_writes+=count;else s->stats.metadata_reads+=count;
    return r;
}
fv_block_result_t fv_auth_format(fv_auth_store *s) {
    if(!s || !s->ready)return FV_BLOCK_ERROR_NOT_READY;
    s->cache_count=0;memset(s->cache,0,sizeof(s->cache));
    for(uint64_t first=0;first<s->metadata_blocks;) {
        uint32_t count=(uint32_t)((s->metadata_blocks-first)>6?6:s->metadata_blocks-first);
        fv_block_result_t r=meta_io(s,first,count,s->cache,true);
        if(r!=FV_BLOCK_OK)return fault(s,r);
        first+=count;
    }
    uint64_t start=now(s);fv_block_result_t r=s->device->ops->sync(s->device);
    s->stats.metadata_us+=now(s)-start;
    return r==FV_BLOCK_OK?r:fault(s,r);
}
static fv_block_result_t validate(fv_auth_store *s,uint64_t lba,uint32_t count,const void *p) {
    if(!s || !p || ((uintptr_t)p&3) || !count || count>64)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if(!s->ready)return FV_BLOCK_ERROR_NOT_READY;
    if(lba>=s->blocks || count>s->blocks-lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    return FV_BLOCK_OK;
}
static fv_block_result_t load(fv_auth_store *s,uint64_t lba,uint32_t count) {
    uint64_t first=lba/15,last=(lba+count-1)/15;
    if(s->cache_count && first>=s->cache_first && last<s->cache_first+s->cache_count)return FV_BLOCK_OK;
    s->cache_count=0;
    uint32_t n=(uint32_t)(last-first+1);
    fv_block_result_t r=meta_io(s,first,n,s->cache,false);
    if(r!=FV_BLOCK_OK)return fault(s,r);
    s->cache_first=first;s->cache_count=n;return FV_BLOCK_OK;
}
static uint8_t *entry(fv_auth_store *s,uint64_t lba) {return s->cache+(size_t)(lba/15-s->cache_first)*512;}
static int tag(fv_auth_store *s,uint64_t lba,const uint8_t *data,uint8_t output[32]) {
    /* Fixed 16-byte domain, 16-byte volume ID, 8-byte LE LBA, ciphertext. */
    uint8_t context[40]={ 'F','V','-','S','E','C','T','O','R','-','M','A','C',0,0,1 };
    memcpy(context+16,s->volume,16);
    for(unsigned i=0;i<8;i++)context[32+i]=(uint8_t)(lba>>(8*i));
    uint64_t start=now(s);int r=fv_hmac_compute(&s->hmac,context,40,data,512,output);
    s->stats.hmac_us+=now(s)-start;return r;
}
fv_block_result_t fv_auth_write(fv_auth_store *s,uint64_t lba,uint32_t count,const uint8_t *data) {
    fv_block_result_t r=validate(s,lba,count,data);if(r!=FV_BLOCK_OK)return r;
    r=load(s,lba,count);if(r!=FV_BLOCK_OK)return r;
    for(uint32_t i=0;i<count;i++) {
        uint8_t *e=entry(s,lba+i);unsigned n=(unsigned)((lba+i)%15);
        if(tag(s,lba+i,data+i*512,e+32+n*32))return fault(s,FV_BLOCK_ERROR_IO);
        e[n]=1;
    }
    uint64_t start=now(s);
    r=s->device->ops->write(s->device,s->data_base+lba,count,data);
    s->stats.data_us+=now(s)-start;
    if(r!=FV_BLOCK_OK)return fault(s,r);
    uint64_t first=lba/15;uint32_t n=(uint32_t)((lba+count-1)/15-first+1);
    r=meta_io(s,first,n,entry(s,lba),true);
    return r==FV_BLOCK_OK?r:fault(s,r);
}
fv_block_result_t fv_auth_read(fv_auth_store *s,uint64_t lba,uint32_t count,uint8_t *data,uint64_t *unset) {
    if(!unset)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    *unset=0;
    fv_block_result_t r=validate(s,lba,count,data);if(r!=FV_BLOCK_OK)return r;
    r=load(s,lba,count);if(r!=FV_BLOCK_OK)goto fail;
    for(uint32_t i=0;i<count;i++) {
        uint8_t state=entry(s,lba+i)[(lba+i)%15];
        if(state==0)*unset|=UINT64_C(1)<<i;
        else if(state!=1){r=FV_BLOCK_ERROR_INTEGRITY;goto fail;}
    }
    for(uint32_t i=0;i<count;) {
        if((*unset>>i)&1) {memset(data+i*512,0,512);i++;continue;}
        uint32_t end=i+1;
        while(end<count && !((*unset>>end)&1))end++;
        uint64_t start=now(s);
        r=s->device->ops->read(s->device,s->data_base+lba+i,end-i,data+i*512);
        s->stats.data_us+=now(s)-start;
        if(r!=FV_BLOCK_OK)goto fail;
        i=end;
    }
    for(uint32_t i=0;i<count;i++) {
        if((*unset>>i)&1)continue;
        uint8_t computed[32];uint8_t *stored=entry(s,lba+i)+32+((lba+i)%15)*32;
        if(tag(s,lba+i,data+i*512,computed)){r=FV_BLOCK_ERROR_IO;goto fail;}
        if(!fv_tag_equal(computed,stored)){r=FV_BLOCK_ERROR_INTEGRITY;goto fail;}
    }
    return FV_BLOCK_OK;
fail:
    memset(data,0,count*512);*unset=0;s->cache_count=0;
    /* Corrupt sectors are local errors; only transport failures close the store. */
    return r==FV_BLOCK_ERROR_INTEGRITY?r:fault(s,r);
}
