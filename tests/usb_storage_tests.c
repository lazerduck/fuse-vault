#include "usb_storage.h"
#include "tusb.h"
#include "fuse_vault/block_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool unlocked,queued,finished,invalidated;
static uint32_t generation=1,writes,reads;
static int failure;
static uint8_t key,asc;
static fv_usb_request request;
static fv_usb_response response;
static uint8_t disk[4*FV_USB_IO_BYTES],data[FV_USB_IO_BYTES];
#ifndef FV_REAL_MSC
bool tud_msc_set_sense(uint8_t lun,uint8_t k,uint8_t a,uint8_t q){(void)lun;(void)q;key=k;asc=a;return true;}
#endif
fv_usb_response fv_usb_rpc(fv_usb_request r){
    if(r.op==FV_USB_LOCK){unlocked=false;++generation;}
    CHECK(!queued); /* Status checks must not serialize each pipelined receive. */
    return (fv_usb_response){.result=(unlocked || r.op==FV_USB_LOCK)?0:FV_BLOCK_ERROR_NOT_READY,.unlocked=unlocked,.blocks=unlocked?sizeof(disk)/512:0,.generation=generation};
}
bool fv_usb_async_submit(fv_usb_request r){
    CHECK(!queued && !finished);CHECK(!((uintptr_t)r.data&3));
    CHECK(r.count && r.count<=FV_USB_IO_BYTES/512);
    request=r;queued=true;return true;
}
static void finish(void){
    CHECK(queued);queued=false;finished=true;
    int result=unlocked?failure:FV_BLOCK_ERROR_NOT_READY;
    if(!result){
        CHECK((uint64_t)(request.lba+request.count)*512<=sizeof(disk));
        if(request.op==FV_USB_WRITE){memcpy(disk+request.lba*512,request.data,request.count*512);++writes;}
        else{memcpy(request.data,disk+request.lba*512,request.count*512);++reads;}
    }else memset(request.data,0xee,request.count*512);
    response=(fv_usb_response){.result=result,.unlocked=unlocked};
}
bool fv_usb_async_take(fv_usb_response *r){if(!finished)return false;*r=response;finished=false;return true;}
void fv_usb_async_wait(void){if(queued)finish();}
void fv_usb_storage_poll(void){if(invalidated){invalidated=false;fv_usb_async_wait();unlocked=false;fv_usb_storage_clear_transport();}}
static bool zero(const uint8_t *p,size_t n){while(n--)if(*p++)return false;return true;}
#ifndef FV_REAL_MSC
int main(void){
    uint32_t blocks,r,w;uint16_t size;const uint32_t sectors=FV_USB_IO_BYTES/512;
    CHECK(!tud_msc_test_unit_ready_cb(0));CHECK(key==2 && asc==0x3a);
    CHECK(!fv_usb_storage_begin(0,true,0,512,512));
    unlocked=true;++generation;
    CHECK(!tud_msc_test_unit_ready_cb(0));CHECK(key==6 && asc==0x28);
    CHECK(tud_msc_test_unit_ready_cb(0));tud_msc_capacity_cb(0,&blocks,&size);CHECK(blocks==sizeof(disk)/512 && size==512);
    CHECK(!fv_usb_storage_begin(0,true,blocks-1,1024,512));CHECK(asc==0x21);
    CHECK(!fv_usb_storage_begin(0,true,0,512,1024));CHECK(asc==0x24);
    CHECK(!fv_usb_storage_begin(1,true,0,512,512));
    /* First chunk returns while worker is held. The USB buffer can be reused. */
    CHECK(fv_usb_storage_begin(0,true,0,2*FV_USB_IO_BYTES,512));
    memset(data,0x5a,sizeof(data));CHECK(tud_msc_write10_cb(0,0,0,data,sizeof(data))==sizeof(data));
    CHECK(queued && !writes && request.data[0]==0x5a);
    CHECK(tud_msc_is_writable_cb(0)); /* Must not wait for SD. */
    memset(data,0xa5,sizeof(data));CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))==0);
    CHECK(request.data[0]==0x5a && data[0]==0xa5);
    finish();CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))==0);
    CHECK(writes==1 && queued && request.data[0]==0xa5);
    CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))==0);
    finish();CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))==sizeof(data));
    CHECK(writes==2 && zero(data,sizeof(data)) && zero(request.data,FV_USB_IO_BYTES));
    CHECK(disk[0]==0x5a && disk[FV_USB_IO_BYTES]==0xa5);
    fv_usb_storage_take_activity(&r,&w);CHECK(!r && w==2*FV_USB_IO_BYTES);
    /* Reads prefetch chunk two while USB still owns chunk one's bytes. */
    CHECK(fv_usb_storage_begin(0,false,0,2*FV_USB_IO_BYTES,512));
    CHECK(tud_msc_read10_cb(0,0,0,data,sizeof(data))==0);finish();
    CHECK(tud_msc_read10_cb(0,0,0,data,sizeof(data))==sizeof(data));
    CHECK(queued && request.lba==sectors && data[0]==0x5a);
    finish();CHECK(data[0]==0x5a);CHECK(tud_msc_read10_cb(0,sectors,0,data,sizeof(data))==sizeof(data));
    CHECK(data[0]==0xa5 && !queued && reads==2);
    fv_usb_storage_take_activity(&r,&w);CHECK(r==2*FV_USB_IO_BYTES && !w);
    fv_usb_storage_take_activity(&r,&w);CHECK(!r && !w);
    /* Short command and short final chunk still wait for durable completion. */
    CHECK(fv_usb_storage_begin(0,true,0,512,512));memset(data,0x44,512);
    CHECK(tud_msc_write10_cb(0,0,0,data,512)==0);finish();CHECK(tud_msc_write10_cb(0,0,0,data,512)==512);
    CHECK(fv_usb_storage_begin(0,true,0,FV_USB_IO_BYTES+512,512));memset(data,0x33,sizeof(data));
    CHECK(tud_msc_write10_cb(0,0,0,data,sizeof(data))==sizeof(data));finish();
    CHECK(tud_msc_write10_cb(0,sectors,0,data,512)==0);finish();CHECK(tud_msc_write10_cb(0,sectors,0,data,512)==512);
    /* Late worker error fails the command rather than acknowledging its tail. */
    CHECK(fv_usb_storage_begin(0,true,0,512,512));failure=FV_BLOCK_ERROR_IO;
    CHECK(tud_msc_write10_cb(0,0,0,data,512)==0);finish();
    CHECK(tud_msc_write10_cb(0,0,0,data,512)<0 && asc==0x0c);CHECK(zero(data,512) && zero(request.data,FV_USB_IO_BYTES));
    CHECK(fv_usb_storage_begin(0,false,0,512,512));failure=FV_BLOCK_ERROR_INTEGRITY;
    CHECK(tud_msc_read10_cb(0,0,0,data,512)==0);finish();
    CHECK(tud_msc_read10_cb(0,0,0,data,512)<0 && asc==0x11);CHECK(zero(data,512));failure=0;
    /* An earlier write failure rejects the next chunk without submitting it. */
    CHECK(fv_usb_storage_begin(0,true,0,2*FV_USB_IO_BYTES,512));failure=FV_BLOCK_ERROR_IO;
    CHECK(tud_msc_write10_cb(0,0,0,data,sizeof(data))==sizeof(data));finish();
    CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))<0 && !queued);failure=0;
    CHECK(fv_usb_storage_begin(0,false,0,512,512));
    CHECK(tud_msc_read10_cb(0,UINT32_MAX,512,data,512)<0 && !queued);
    CHECK(tud_msc_read10_cb(0,0,1,data,512)<0 && !queued);
    CHECK(tud_msc_read10_cb(0,0,0,data,511)<0 && !queued);
    CHECK(tud_msc_read10_cb(0,0,0,data,FV_USB_IO_BYTES+512)<0 && !queued);
    /* Invalidation drains work, wipes both buffers and suppresses stale reads. */
    CHECK(fv_usb_storage_begin(0,false,0,512,512));CHECK(tud_msc_read10_cb(0,0,0,data,512)==0);
    invalidated=true;CHECK(tud_msc_read10_cb(0,0,0,data,512)<0);CHECK(!queued && !finished && zero(data,512));
    unlocked=true;CHECK(fv_usb_storage_begin(0,true,0,2*FV_USB_IO_BYTES,512));memset(data,0x66,sizeof(data));
    CHECK(tud_msc_write10_cb(0,0,0,data,sizeof(data))==sizeof(data));fv_usb_storage_clear_transport();
    CHECK(!queued && !finished && zero(request.data,FV_USB_IO_BYTES));CHECK(tud_msc_write10_cb(0,sectors,0,data,sizeof(data))<0);
    uint8_t command[16]={0x35};CHECK(tud_msc_scsi_cb(0,command,NULL,0)==0);
    CHECK(tud_msc_start_stop_cb(0,0,false,true));CHECK(!unlocked);
    puts("USB pipelining, completion, error and buffer ownership checks passed");return 0;
}

#endif
