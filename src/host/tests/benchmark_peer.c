/* Desktop transport peer: the real engine over stdin/stdout and a temporary
 * file-backed block device. Used to test the laptop runner without hardware. */
#include "fuse_vault/benchmark.h"
#include <stdio.h>
#include <string.h>
#include <stdalign.h>
static FILE *disk;
static uint64_t tick;
static bool open_disk(void *context) {(void)context;return disk!=NULL;}
static uint64_t capacity(const fv_block_device_t *d) {(void)d;return 9000;}
static uint64_t now(void) {return ++tick;}
static fv_block_result_t io(fv_block_device_t *d,uint64_t lba,uint32_t n,void *p,bool write) {
    (void)d;
    if(lba+n>9000 || fseek(disk,(long)(lba*512),SEEK_SET))return FV_BLOCK_ERROR_IO;
    size_t done=write?fwrite(p,512,n,disk):fread(p,512,n,disk);
    if(done!=n || (write && fflush(disk)))return FV_BLOCK_ERROR_IO;
    return FV_BLOCK_OK;
}
static fv_block_result_t read_disk(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *p){return io(d,lba,n,p,false);}
static fv_block_result_t write_disk(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *p){return io(d,lba,n,(void *)p,true);}
static fv_block_result_t sync_disk(fv_block_device_t *d){(void)d;return fflush(disk)?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
int main(void) {
    disk=tmpfile();if(!disk)return 1;
    const fv_block_device_ops_t ops={.read=read_disk,.write=write_disk,.sync=sync_disk,.block_count=capacity};
    fv_block_device_t device={.ops=&ops};
    fv_bench_engine engine={.device=&device,.open=open_disk,.now_us=now};
    static alignas(4) uint8_t data[FV_BENCH_BUFFER_BYTES];
    uint8_t header[FV_BENCH_RESPONSE_BYTES];
    while(fread(header,1,32,stdin)==32) {
        fv_bench_request request;fv_bench_response response;
        if(!fv_bench_decode(header,&request))return 2;
        if(fread(data,1,request.payload_bytes,stdin)!=request.payload_bytes)return 3;
        fv_bench_execute(&engine,&request,data,&response);
        fv_bench_encode(&response,header);
        if(fwrite(header,1,FV_BENCH_RESPONSE_BYTES,stdout)!=FV_BENCH_RESPONSE_BYTES || fwrite(data,1,response.payload_bytes,stdout)!=response.payload_bytes || fflush(stdout))return 4;
    }
    fv_bench_reset(&engine);fclose(disk);return 0;
}
