#include "fuse_vault/benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static uint8_t disk[128*512];
static unsigned writes,reads,syncs,last_count;
static bool opened,fail_io,fail_open;
static uint64_t tick;
static uint64_t now(void){return ++tick;}
static bool open_disk(void *unused){(void)unused;opened=!fail_open;return opened;}
static uint64_t capacity(const fv_block_device_t *d){(void)d;return opened?128:0;}
static fv_block_result_t read_disk(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *p) {
    (void)d;CHECK(lba+n<=128);reads++;last_count=n;
    if(fail_io)return FV_BLOCK_ERROR_IO;
    memcpy(p,disk+lba*512,n*512);return FV_BLOCK_OK;
}
static fv_block_result_t write_disk(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *p) {
    (void)d;CHECK(lba+n<=128);writes++;last_count=n;
    if(fail_io)return FV_BLOCK_ERROR_IO;
    memcpy(disk+lba*512,p,n*512);return FV_BLOCK_OK;
}
static fv_block_result_t sync_disk(fv_block_device_t *d){(void)d;syncs++;return fail_io?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static const fv_block_device_ops_t ops={.read=read_disk,.write=write_disk,.sync=sync_disk,.block_count=capacity};
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int main(void) {
    fv_block_device_t device={.ops=&ops};
    fv_bench_engine engine={.device=&device,.open=open_disk,.now_us=now,.cpu_hz=150000000,.sd_hz=25000000};
    static alignas(4) uint8_t data[FV_BENCH_BUFFER_BYTES],plain[FV_BENCH_BUFFER_BYTES];
    for(unsigned i=0;i<sizeof(plain);i++)plain[i]=(uint8_t)(i*7+11);
    fv_bench_response response;
    fv_bench_request request={.op=FV_BENCH_WRITE,.blocks=1,.payload_bytes=512};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_NOT_READY && writes==0);
    uint32_t stacks[]={0,1,2,0x0201,0x0102,0x02010201};
    unsigned sizes[]={1,8,16,32,64};
    for(unsigned a=0;a<sizeof(stacks)/sizeof(stacks[0]);a++) {
        request=(fv_bench_request){.op=FV_BENCH_CONFIG,.sequence=42,.lba=FV_BENCH_WRITE_TOKEN,.blocks=128,.algorithms=stacks[a]};
        fv_bench_execute(&engine,&request,data,&response);
        CHECK(response.status==0 && response.sequence==42 && response.capacity_blocks==128);
        for(unsigned b=0;b<sizeof(sizes)/sizeof(sizes[0]);b++) {
            unsigned n=sizes[b];memcpy(data,plain,n*512);
            unsigned old_reads=reads,old_writes=writes;
            request=(fv_bench_request){.op=FV_BENCH_WRITE,.lba=128-n,.blocks=n,.payload_bytes=n*512};
            fv_bench_execute(&engine,&request,data,&response);
            CHECK(response.status==0 && response.payload_bytes==0 && writes==old_writes+1 && reads==old_reads && last_count==n);
            CHECK((memcmp(plain,disk+(128-n)*512,n*512)!=0)==(stacks[a]!=0));
            CHECK(response.sd_us && (stacks[a]?response.crypto_us>0:response.crypto_us==0));
            memset(data,0,n*512);
            request.op=FV_BENCH_READ;request.payload_bytes=0;
            fv_bench_execute(&engine,&request,data,&response);
            CHECK(response.status==0 && response.payload_bytes==n*512 && reads==old_reads+1);
            CHECK(!memcmp(data,plain,n*512));
        }
    }
    request=(fv_bench_request){.op=FV_BENCH_WRITE,.lba=127,.blocks=2,.payload_bytes=1024};
    unsigned old_writes=writes;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_INVALID && writes==old_writes);
    request.lba=0;request.blocks=65;request.payload_bytes=65*512;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_INVALID);
    request.blocks=1;request.payload_bytes=1;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_INVALID);
    request.payload_bytes=512;fail_io=true;
    fv_bench_execute(&engine,&request,data,&response);
    CHECK(response.status==FV_BENCH_IO && !engine.configured && response.payload_bytes==0);
    for(unsigned i=0;i<512;i++)CHECK(data[i]==0);
    fail_io=false;
    request=(fv_bench_request){.op=FV_BENCH_CONFIG,.blocks=128,.algorithms=1};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_INVALID);
    request.lba=FV_BENCH_WRITE_TOKEN;request.algorithms=0x010001;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_INVALID);
    request.algorithms=1;fail_open=true;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_NOT_READY && !engine.configured);
    fail_open=false;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0);
    request=(fv_bench_request){.op=FV_BENCH_READ,.blocks=1};fail_io=true;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==FV_BENCH_IO && response.payload_bytes==0 && !engine.configured);
    fail_io=false;
    request=(fv_bench_request){.op=FV_BENCH_CONFIG,.lba=FV_BENCH_WRITE_TOKEN,.blocks=128};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0);
    request=(fv_bench_request){.op=FV_BENCH_END};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0 && syncs==1 && !engine.configured);
    request=(fv_bench_request){.op=FV_BENCH_AUTH_CONFIG,.lba=FV_BENCH_WRITE_TOKEN,.blocks=100,.algorithms=0x0201};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0 && response.metadata_writes==7);
    request=(fv_bench_request){.op=FV_BENCH_READ,.blocks=64};
    memset(data,0x81,sizeof(data));
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0 && response.payload_bytes==32768);
    for(size_t i=0;i<sizeof(data);i++)CHECK(data[i]==0); /* Never decrypt unset zeros. */
    memcpy(data,plain,sizeof(data));
    request=(fv_bench_request){.op=FV_BENCH_WRITE,.blocks=64,.payload_bytes=32768};
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0 && response.hmac_us>0 && response.metadata_writes==5);
    disk[7*512]^=1; /* Alter ciphertext of LBA 0 after its successful write. */
    request=(fv_bench_request){.op=FV_BENCH_READ,.blocks=64};
    fv_bench_execute(&engine,&request,data,&response);
    CHECK(response.status==FV_BENCH_INTEGRITY && response.payload_bytes==0 && response.crypto_us==0 && engine.configured);
    for(size_t i=0;i<sizeof(data);i++)CHECK(data[i]==0);
    request.lba=1;request.blocks=1;
    fv_bench_execute(&engine,&request,data,&response);CHECK(response.status==0 && !memcmp(data,plain+512,512));
    fv_bench_reset(&engine);
    uint8_t header[FV_BENCH_RESPONSE_BYTES]={0};
    put32(header,FV_BENCH_MAGIC);put32(header+4,FV_BENCH_VERSION);put32(header+8,FV_BENCH_WRITE);
    put32(header+12,17);put32(header+20,64);put32(header+24,32768);
    CHECK(fv_bench_decode(header,&request) && request.sequence==17 && request.blocks==64);
    put32(header+24,32769);CHECK(!fv_bench_decode(header,&request));
    put32(header+24,32768);put32(header+20,UINT32_MAX);CHECK(!fv_bench_decode(header,&request));
    put32(header+8,FV_BENCH_READ);put32(header+24,1);CHECK(!fv_bench_decode(header,&request));
    header[0]=0;CHECK(!fv_bench_decode(header,&request));
    response=(fv_bench_response){.op=4,.sequence=17,.crypto_us=UINT64_C(0x123456789abcdef0)};
    fv_bench_encode(&response,header);
    CHECK(header[8]==4 && header[11]==128 && header[12]==17 && header[24]==0xf0 && header[31]==0x12);
    engine.hmac_checked=true;engine.hmac_ok=false;
    request=(fv_bench_request){.op=FV_BENCH_AUTH_CONFIG,.lba=FV_BENCH_WRITE_TOKEN,.blocks=100};
    old_writes=writes;
    fv_bench_execute(&engine,&request,data,&response);
    CHECK(response.status==FV_BENCH_CRYPTO && response.hmac_self_test==0 && writes==old_writes);
    puts("PASS: complete raw/encrypted flows, batches, storage bounds, write acknowledgments, failures and wire encoding");
    return 0;
}
