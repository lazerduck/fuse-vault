#include "fuse_vault/benchmark.h"
#include <string.h>
void fv_bench_reset(fv_bench_engine *e) {
    e->authenticated=false;
    fv_auth_close(&e->store);
    e->configured=false;
    e->configured_blocks=0;
    fv_pipeline_clear(&e->pipeline);
}
static uint32_t configure(fv_bench_engine *e,const fv_bench_request *q,fv_bench_response *r) {
    fv_bench_reset(e);
    if(q->lba!=FV_BENCH_WRITE_TOKEN || !q->blocks || q->blocks>FV_BENCH_SCRATCH_BLOCKS)
        return FV_BENCH_INVALID;
    fv_algorithm algorithms[FV_MAX_LAYERS];
    uint8_t keys[FV_MAX_LAYERS][FV_XTS_KEY_BYTES];
    size_t count=0;
    uint32_t packed=q->algorithms;
    /* Empty stack means raw SD baseline. Other stacks contain 1..4 IDs. */
    while(packed) {
        uint32_t algorithm=packed&255u;
        if(algorithm!=FV_AES_256_XTS && algorithm!=FV_CAMELLIA_256_XTS) return FV_BENCH_INVALID;
        algorithms[count]=(fv_algorithm)algorithm;
        for(unsigned i=0;i<FV_XTS_KEY_BYTES;i++) keys[count][i]=(uint8_t)(i+71u*count);
        count++; packed>>=8;
    }
    uint64_t start=e->now_us();
    if(count && fv_pipeline_init(&e->pipeline,algorithms,(const uint8_t (*)[64])keys,count)!=FV_OK)
        return FV_BENCH_CRYPTO;
    r->setup_us=e->now_us()-start;
    if(!e->open(e->open_context)) {fv_bench_reset(e);return FV_BENCH_NOT_READY;}
    r->capacity_blocks=e->device->ops->block_count(e->device);
    if(q->blocks>r->capacity_blocks) {fv_bench_reset(e);return FV_BENCH_INVALID;}
    if(q->op==FV_BENCH_AUTH_CONFIG) {
        uint8_t key[32],volume[16];
        /* Public benchmark-only material, distinct from all layer test keys. */
        for(unsigned i=0;i<32;i++)key[i]=(uint8_t)(0xd3^i);
        for(unsigned i=0;i<16;i++)volume[i]=(uint8_t)(0x80+i);
        uint64_t auth_start=e->now_us();
        fv_block_result_t result=fv_auth_open(&e->store,e->device,0,q->blocks,volume,key,e->now_us);
        r->setup_us+=e->now_us()-auth_start;
        if(result==FV_BLOCK_OK)result=fv_auth_format(&e->store);
        r->metadata_us=e->store.stats.metadata_us;
        r->metadata_writes=e->store.stats.metadata_writes;
        if(result!=FV_BLOCK_OK){fv_bench_reset(e);return FV_BENCH_IO;}
        e->authenticated=true;
    }
    e->configured_blocks=q->blocks;
    e->configured=true;
    return FV_BENCH_OK;
}
void fv_bench_execute(fv_bench_engine *e,const fv_bench_request *q,uint8_t buffer[FV_BENCH_BUFFER_BYTES],fv_bench_response *r) {
    *r=(fv_bench_response){.op=q->op,.sequence=q->sequence,
        .cpu_hz=e->cpu_hz,.sd_hz=e->sd_hz};
    if(!e->hmac_checked) {
        e->hmac_ok=fv_hmac_self_test();e->hmac_checked=true;
    }
    r->hmac_backend=fv_hmac_backend();r->hmac_self_test=e->hmac_ok?1:0;
    if(!e->hmac_ok){r->status=FV_BENCH_CRYPTO;return;}
    r->capacity_blocks=e->device->ops->block_count(e->device);
    if(q->op==FV_BENCH_INFO || q->op==FV_BENCH_END) {
        if(q->lba || q->blocks || q->payload_bytes || q->algorithms) {r->status=FV_BENCH_INVALID;return;}
        if(q->op==FV_BENCH_END) {
            if(e->configured) {
                uint64_t start=e->now_us();
                if(e->device->ops->sync(e->device)!=FV_BLOCK_OK) r->status=FV_BENCH_IO;
                r->sd_us=e->now_us()-start;
            }
            fv_bench_reset(e);
        }
        return;
    }
    if((q->op==FV_BENCH_CONFIG || q->op==FV_BENCH_AUTH_CONFIG) && !q->payload_bytes) {
        r->status=configure(e,q,r); return;
    }
    if(q->op!=FV_BENCH_READ && q->op!=FV_BENCH_WRITE) {r->status=FV_BENCH_INVALID;return;}
    if(!e->configured) {r->status=FV_BENCH_NOT_READY;return;}
    bool write=q->op==FV_BENCH_WRITE;
    if(!q->blocks || q->blocks>64 || q->lba>=e->configured_blocks ||
       q->blocks>e->configured_blocks-q->lba || q->algorithms ||
       q->payload_bytes!=(write?q->blocks*512:0)) {r->status=FV_BENCH_INVALID;return;}
    uint64_t unset=0;
    e->store.stats=(fv_auth_stats){0};
    for(unsigned stage=0;stage<2;stage++) {
        bool crypto=write?(stage==0):(stage==1);
        if(crypto && !e->pipeline.count) continue;
        uint64_t start=e->now_us();
        if(crypto) {
            if(e->pipeline.count) {
                for(uint32_t n=0;n<q->blocks;n++) {
                    if(!write && ((unset>>n)&1))continue;
                    uint8_t *sector=buffer+n*512;
                    fv_status result=write?fv_pipeline_encrypt(&e->pipeline,q->lba+n,sector,sector):
                                           fv_pipeline_decrypt(&e->pipeline,q->lba+n,sector,sector);
                    if(result!=FV_OK) {r->status=FV_BENCH_CRYPTO;break;}
                }
            }
            r->crypto_us=e->now_us()-start;
        } else {
            fv_block_result_t result;
            if(e->authenticated) {
                result=write?fv_auth_write(&e->store,q->lba,q->blocks,buffer):
                             fv_auth_read(&e->store,q->lba,q->blocks,buffer,&unset);
                r->sd_us=e->store.stats.data_us;
                r->hmac_us=e->store.stats.hmac_us;
                r->metadata_us=e->store.stats.metadata_us;
                r->metadata_reads=e->store.stats.metadata_reads;
                r->metadata_writes=e->store.stats.metadata_writes;
            } else {
                result=write?e->device->ops->write(e->device,q->lba,q->blocks,buffer):
                             e->device->ops->read(e->device,q->lba,q->blocks,buffer);
                r->sd_us=e->now_us()-start;
            }
            if(result!=FV_BLOCK_OK)r->status=result==FV_BLOCK_ERROR_INTEGRITY?FV_BENCH_INTEGRITY:FV_BENCH_IO;
        }
        if(r->status) {
            memset(buffer,0,q->blocks*512);
            if(r->status!=FV_BENCH_INTEGRITY)fv_bench_reset(e);
            return;
        }
    }
    if(!write) r->payload_bytes=q->blocks*512;
}
