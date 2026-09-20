#include "fuse_vault/auth_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned calls,fail_at,syncs;
static uint64_t written,last;
static bool fail_sync;
static uint64_t capacity(const fv_block_device_t *d){(void)d;return 4096;}
static fv_block_result_t read_unused(fv_block_device_t *d,uint64_t l,uint32_t n,uint8_t *p){(void)d;(void)l;(void)n;(void)p;return FV_BLOCK_ERROR_IO;}
static fv_block_result_t write_zero(fv_block_device_t *d,uint64_t l,uint32_t n,const uint8_t *p){
    (void)d;CHECK(l==17+written);CHECK(n==64 || n==9);CHECK(l+n<=154);
    for(unsigned i=0;i<n*512;i++)CHECK(!p[i]);
    if(++calls==fail_at)return FV_BLOCK_ERROR_IO;
    written+=n;return FV_BLOCK_OK;
}
static fv_block_result_t sync_disk(fv_block_device_t *d){(void)d;++syncs;return fail_sync?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static void progress(void *context,uint64_t completed,uint64_t total){
    CHECK(context==&last);CHECK(total==137);CHECK(completed==written);CHECK(completed>=last);last=completed;
}
int main(void){
    fv_block_device_ops_t ops={read_unused,write_zero,sync_disk,capacity,NULL};fv_block_device_t d={&ops,NULL};
    uint8_t volume[16]={1},key[32]={2};alignas(4) uint8_t scratch[64*512];fv_auth_store s;
    for(unsigned failure=0;failure<3;failure++){
        calls=syncs=0;written=last=0;fail_at=failure==1?2:0;fail_sync=failure==2;
        CHECK(fv_auth_open(&s,&d,17,2055,volume,key,NULL)==FV_BLOCK_OK);
        memset(scratch,0xa5,sizeof(scratch));
        fv_block_result_t r=fv_auth_format_buffered(&s,scratch,64,progress,&last);
        if(failure==1){CHECK(r==FV_BLOCK_ERROR_IO && last==64 && !syncs && !s.ready);}
        else if(failure==2){CHECK(r==FV_BLOCK_ERROR_IO && last==137 && syncs==1 && !s.ready);}
        else {CHECK(r==FV_BLOCK_OK && calls==3 && last==137 && syncs==1);}
    }
    CHECK(fv_auth_open(&s,&d,17,2055,volume,key,NULL)==FV_BLOCK_OK);
    CHECK(fv_auth_format_buffered(&s,scratch,0,progress,&last)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
    CHECK(fv_auth_format_buffered(&s,scratch+1,1,progress,&last)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
    fv_auth_close(&s);puts("Buffered metadata format: boundaries, progress, write and sync failures passed");
}
