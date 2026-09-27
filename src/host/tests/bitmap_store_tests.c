#include "fuse_vault/auth_store.h"
#include "fuse_vault/volume_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define CAP 70000u
#define BLOCKS 65000u
static uint8_t *disk,*durable,dirty[CAP];
static unsigned operation,fail_operation,metadata_reads,writes;
static bool fail_after;
static uint64_t formatted;
static fv_auth_store store;
static uint8_t volume[16]={1},key[32]={2};
static alignas(4) uint8_t input[64*512],output[64*512];
static uint64_t capacity(const fv_block_device_t *d){(void)d;return CAP;}
static fv_block_result_t read_disk(fv_block_device_t *d,uint64_t l,uint32_t n,uint8_t *out){
    (void)d;CHECK(l+n<=CAP);
    if(l>=store.base && l<store.data_base)++metadata_reads;
    memcpy(out,disk+l*512,n*512);return FV_BLOCK_OK;
}
static fv_block_result_t write_disk(fv_block_device_t *d,uint64_t l,uint32_t n,const uint8_t *in){
    (void)d;CHECK(l+n<=CAP);++operation;++writes;
    if(operation==fail_operation && !fail_after)return FV_BLOCK_ERROR_IO;
    memcpy(disk+l*512,in,n*512);memset(dirty+l,1,n);
    return operation==fail_operation?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;
}
static fv_block_result_t sync_disk(fv_block_device_t *d){
    (void)d;++operation;
    if(operation==fail_operation && !fail_after)return FV_BLOCK_ERROR_IO;
    for(unsigned i=0;i<CAP;i++)if(dirty[i]){memcpy(durable+i*512,disk+i*512,512);dirty[i]=0;}
    return operation==fail_operation?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;
}
static fv_block_device_ops_t ops={read_disk,write_disk,sync_disk,capacity,NULL};
static fv_block_device_t device={&ops,NULL};
static void reopen(void){CHECK(fv_auth_open_bitmap(&store,&device,16,BLOCKS,volume,key,NULL)==FV_BLOCK_OK);}
static void progress(void *c,uint64_t done,uint64_t total){(void)c;CHECK(total==2);CHECK(done>=formatted);formatted=done;}
static void fresh(void){
    memset(disk,0xa7,CAP*512);memcpy(durable,disk,CAP*512);memset(dirty,0,sizeof(dirty));
    operation=fail_operation=writes=metadata_reads=0;formatted=0;reopen();
    CHECK(store.bitmap_blocks==2 && store.base==18);
    CHECK(fv_auth_format_buffered(&store,output,64,progress,NULL)==FV_BLOCK_OK);
    CHECK(writes==1 && formatted==2);CHECK(disk[store.base*512]==0xa7);CHECK(disk[store.data_base*512]==0xa7);
    operation=0;
}
static void restart(void){memcpy(disk,durable,CAP*512);memset(dirty,0,sizeof(dirty));fail_operation=0;reopen();}
static void verify(uint64_t lba,uint32_t count){
    uint64_t unset=0;CHECK(fv_auth_read(&store,lba,count,output,&unset)==FV_BLOCK_OK);
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<512;j++)CHECK(output[i*512+j]==((unset>>i)&1?0:0x53));
}
int main(void){
    disk=malloc(CAP*512);durable=malloc(CAP*512);CHECK(disk && durable);memset(input,0x53,sizeof(input));
    fresh();verify(0,64);CHECK(metadata_reads==0); /* Old bytes are not read at all. */
    CHECK(fv_auth_write(&store,0,1,input)==FV_BLOCK_OK);restart();verify(0,15);
    unsigned before=operation;CHECK(fv_auth_write(&store,1,1,input)==FV_BLOCK_OK);
    CHECK(operation-before==2); /* Already initialized: data+tags only, no bitmap write. */
    CHECK(sync_disk(&device)==FV_BLOCK_OK);restart();verify(0,15);
    CHECK(fv_auth_write(&store,30,1,input)==FV_BLOCK_OK);restart();verify(0,45);
    CHECK((disk[16*512]&5)==5); /* Publishing another region preserves old bits. */
    /* First write crosses metadata-sector 4095/4096, hence two bitmap sectors.
     * Cut before/after every write and barrier, then reopen from durable bytes. */
    const uint64_t lba=4095u*15u+13u;
    for(unsigned after=0;after<2;after++)for(unsigned step=1;step<=7;step++){
        fresh();fail_operation=step;fail_after=after;
        CHECK(fv_auth_write(&store,lba,64,input)==FV_BLOCK_ERROR_IO);CHECK(!store.ready);
        restart();verify(lba,64);verify(0,15);
    }
    fresh();CHECK(fv_auth_write(&store,lba,64,input)==FV_BLOCK_OK);restart();verify(lba,64);
    CHECK(disk[16*512+511]&128);CHECK(disk[17*512]&1);
    /* Lost bitmap bits are deletion under the existing threat model; forged bits
     * must not bypass ciphertext authentication. */
    disk[16*512]=0;reopen();verify(0,15);
    disk[16*512]|=1;reopen();uint64_t unset=0;
    CHECK(fv_auth_read(&store,0,1,output,&unset)==FV_BLOCK_ERROR_INTEGRITY);
    /* Valid new ciphertext with a forged tag must also fail. */
    fresh();CHECK(fv_auth_write(&store,0,1,input)==FV_BLOCK_OK);
    disk[store.base*512+32]^=1;reopen();CHECK(fv_auth_read(&store,0,1,output,&unset)==FV_BLOCK_ERROR_INTEGRITY);
    fv_volume_descriptor d={.layout_version=2,.volume_id={1},.logical_blocks=2048,.layer_count=1,.cipher_ids={1}},decoded;
    uint8_t encoded[128],bad[128];CHECK(fv_volume_descriptor_encode(&d,4250,encoded));
    CHECK(!fv_volume_descriptor_encode(&d,4249,bad));CHECK(fv_volume_descriptor_decode(encoded,128,4250,&decoded));CHECK(decoded.layout_version==2);
    const unsigned fields[]={8,48,56,64,100,108,116};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++){memcpy(bad,encoded,128);bad[fields[i]]^=1;CHECK(!fv_volume_descriptor_decode(bad,128,UINT64_MAX,&decoded));}
    d.layout_version=1;CHECK(fv_volume_descriptor_encode(&d,4249,encoded));CHECK(fv_volume_descriptor_decode(encoded,128,4249,&decoded));CHECK(decoded.layout_version==1);
    free(disk);free(durable);puts("Bitmap initialization, boundaries, crash ordering, integrity and format version checks passed");
}
