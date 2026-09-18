#include "fuse_vault/auth_store.h"
#include <openssl/hmac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static alignas(4) uint8_t disk[160][512],data[64*512],saved[64*512];
static unsigned read_calls,write_calls,fail_write;
static uint64_t ticks;
static uint64_t now(void){return ++ticks;}
static uint64_t capacity(const fv_block_device_t *d){(void)d;return 160;}
static fv_block_result_t read_disk(fv_block_device_t *d,uint64_t l,uint32_t n,uint8_t *p) {
    (void)d;CHECK(l+n<=160);read_calls++;memcpy(p,disk[l],n*512);return FV_BLOCK_OK;
}
static fv_block_result_t write_disk(fv_block_device_t *d,uint64_t l,uint32_t n,const uint8_t *p) {
    (void)d;CHECK(l+n<=160);write_calls++;
    if(fail_write==write_calls)return FV_BLOCK_ERROR_IO;
    memcpy(disk[l],p,n*512);return FV_BLOCK_OK;
}
static fv_block_result_t sync_disk(fv_block_device_t *d){(void)d;return FV_BLOCK_OK;}
static const fv_block_device_ops_t ops={.read=read_disk,.write=write_disk,.sync=sync_disk,.block_count=capacity};
static fv_block_device_t device={.ops=&ops};
static uint8_t key[32],volume[16];
static void reopen(fv_auth_store *s){CHECK(fv_auth_open(s,&device,3,128,volume,key,now)==FV_BLOCK_OK);}
static void zeros(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)CHECK(p[i]==0);}
static void hmac_tests(void) {
    fv_hmac h;uint8_t k[131],actual[32],expected[32];unsigned length;
    memset(k,0x0b,20);CHECK(fv_hmac_init(&h,k,20)==0);
    CHECK(fv_hmac_compute(&h,NULL,0,(const uint8_t *)"Hi There",8,actual)==0);
    const uint8_t rfc4231[]={0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    CHECK(fv_tag_equal(actual,rfc4231));
    for(unsigned i=0;i<131;i++)k[i]=(uint8_t)(i*7);
    for(unsigned i=0;i<512;i++)data[i]=(uint8_t)i;
    unsigned sizes[]={1,32,64,65,131};
    for(unsigned i=0;i<5;i++) {
        CHECK(fv_hmac_init(&h,k,sizes[i])==0);
        CHECK(fv_hmac_compute(&h,data,40,data+40,472,actual)==0);
        CHECK(HMAC(EVP_sha256(),k,(int)sizes[i],data,512,expected,&length));
        CHECK(length==32 && fv_tag_equal(actual,expected));
        expected[31]^=1;CHECK(!fv_tag_equal(actual,expected));
        CHECK(fv_hmac_compute(&h,data,512,NULL,0,expected)==0 && fv_tag_equal(actual,expected));
    }
    fv_hmac_clear(&h);zeros((const uint8_t *)&h,sizeof(h));
}
int main(void) {
    hmac_tests();
    for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    memset(volume,0x91,16);memset(disk,0xa5,sizeof(disk));
    fv_auth_store store;reopen(&store);
    CHECK(store.metadata_blocks==9 && store.data_base==12);
    CHECK(fv_auth_format(&store)==FV_BLOCK_OK);
    CHECK(disk[2][0]==0xa5 && disk[12][0]==0xa5); /* No payload initialization. */
    uint64_t unset;unsigned before=read_calls;
    CHECK(fv_auth_read(&store,14,64,data,&unset)==FV_BLOCK_OK && unset==UINT64_MAX);
    CHECK(read_calls==before+1);zeros(data,sizeof(data)); /* Metadata only, no data reads. */
    for(unsigned i=0;i<sizeof(saved);i++)saved[i]=(uint8_t)(i*13+5);
    before=read_calls;unsigned writes=write_calls;
    CHECK(fv_auth_write(&store,14,64,saved)==FV_BLOCK_OK);
    CHECK(read_calls==before && write_calls==writes+2); /* One data and one batched metadata write. */
    CHECK(fv_auth_read(&store,14,64,data,&unset)==FV_BLOCK_OK && unset==0 && !memcmp(data,saved,sizeof(data)));
    CHECK(fv_auth_write(&store,0,1,saved)==FV_BLOCK_OK);
    CHECK(fv_auth_write(&store,127,1,saved)==FV_BLOCK_OK);
    reopen(&store);
    CHECK(fv_auth_read(&store,0,16,data,&unset)==FV_BLOCK_OK);
    CHECK(unset==UINT64_C(0x3ffe)); /* 0 and 14,15 written, 1..13 unset. */
    CHECK(!memcmp(data,saved,512));zeros(data+512,13*512);
    CHECK(fv_auth_read(&store,127,1,data,&unset)==FV_BLOCK_OK && !memcmp(data,saved,512));
    CHECK(fv_auth_read(&store,128,1,data,&unset)==FV_BLOCK_ERROR_OUT_OF_RANGE);
    CHECK(fv_auth_write(&store,127,2,saved)==FV_BLOCK_ERROR_OUT_OF_RANGE);
    CHECK(fv_auth_write(&store,0,65,saved)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
    CHECK(fv_auth_write(&store,0,1,saved+1)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
    /* Corruption and volume/key mismatch release no bytes; reopen drops cache. */
    disk[12][5]^=1;reopen(&store);
    CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);zeros(data,512);
    disk[12][5]^=1;
    disk[3][32]^=1;reopen(&store);
    CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);zeros(data,512);
    disk[3][32]^=1;
    key[0]^=1;reopen(&store);CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);key[0]^=1;
    volume[0]^=1;reopen(&store);CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);volume[0]^=1;
    /* Moving a valid ciphertext/tag pair to a different logical sector fails. */
    memcpy(disk[13],disk[12],512);disk[3][1]=1;memcpy(disk[3]+64,disk[3]+32,32);reopen(&store);
    CHECK(fv_auth_read(&store,1,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);zeros(data,512);
    /* Explicit deletion is allowed; malformed written-state bytes are not unset. */
    disk[3][0]=0;reopen(&store);before=read_calls;
    CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_OK && unset==1 && read_calls==before+1);zeros(data,512);
    disk[3][0]=2;reopen(&store);CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);
    /* Replay at the original LBA is accepted by policy. */
    disk[3][0]=1;reopen(&store);
    uint8_t old_data[512],old_tag[32];memcpy(old_data,disk[12],512);memcpy(old_tag,disk[3]+32,32);
    memset(data,0x31,512);CHECK(fv_auth_write(&store,0,1,data)==FV_BLOCK_OK);
    memcpy(disk[12],old_data,512);memcpy(disk[3]+32,old_tag,32);reopen(&store);
    CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_OK && !memcmp(data,old_data,512));
    /* Power loss after new payload but before its tag: no success, then integrity failure. */
    memset(data,0xf1,512);fail_write=write_calls+2;
    CHECK(fv_auth_write(&store,0,1,data)==FV_BLOCK_ERROR_IO && !store.ready);
    fail_write=0;reopen(&store);
    CHECK(fv_auth_read(&store,0,1,data,&unset)==FV_BLOCK_ERROR_INTEGRITY);zeros(data,512);
    CHECK(fv_auth_read(&store,127,1,data,&unset)==FV_BLOCK_OK); /* Unaffected data still usable. */
    /* Failed data write must not commit a new tag. */
    uint8_t metadata[512];memcpy(metadata,disk[11],512);fail_write=write_calls+1;
    CHECK(fv_auth_write(&store,127,1,saved)==FV_BLOCK_ERROR_IO);
    CHECK(!memcmp(metadata,disk[11],512));fail_write=0;
    CHECK(fv_auth_open(&store,&device,UINT64_MAX,1,volume,key,now)==FV_BLOCK_ERROR_OUT_OF_RANGE);
    CHECK(fv_auth_open(&store,&device,0,UINT64_MAX,volume,key,now)==FV_BLOCK_ERROR_OUT_OF_RANGE);
    puts("PASS: HMAC vectors, packed tags, unset sectors, cache/batches, corruption, relocation, replay and interrupted writes");
    return 0;
}
