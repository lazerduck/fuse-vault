#include "usb_storage.h"
#include "tusb.h"
#include "fuse_vault/block_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool unlocked;
static uint32_t generation=1,calls,last_lba,last_count,writes;
static int failure;
static uint8_t key,asc;
static uint8_t *last_scratch;
static uint8_t disk[16*512];
bool tud_msc_set_sense(uint8_t lun,uint8_t k,uint8_t a,uint8_t q){(void)lun;(void)q;key=k;asc=a;return true;}
fv_usb_response fv_usb_rpc(fv_usb_request r) {
    ++calls;int result=0;
    if(r.op==FV_USB_LOCK){unlocked=false;++generation;}
    else if(r.op==FV_USB_READ || r.op==FV_USB_WRITE) {
        last_scratch=r.data;last_lba=r.lba;last_count=r.count;
        CHECK(!((uintptr_t)r.data&3));
        if(!unlocked)result=FV_BLOCK_ERROR_NOT_READY;
        else if(r.lba>=16 || r.count>16-r.lba)result=FV_BLOCK_ERROR_OUT_OF_RANGE;
        else if(failure) {
            /* Simulate an implementation that leaves plaintext behind on failure. */
            memset(r.data,0xee,r.count*512);result=failure;
        } else if(r.op==FV_USB_WRITE){memcpy(disk+r.lba*512,r.data,r.count*512);++writes;}
        else memcpy(r.data,disk+r.lba*512,r.count*512);
    } else if(r.op==FV_USB_SYNC && !unlocked)result=FV_BLOCK_ERROR_NOT_READY;
    return (fv_usb_response){.result=result,.unlocked=unlocked,.blocks=unlocked?16:0,.generation=generation};
}
static bool zero(const uint8_t *p,size_t n){while(n--)if(*p++)return false;return true;}
int main(void) {
    uint8_t data[FV_USB_IO_BYTES];uint32_t blocks;uint16_t size;
    CHECK(!tud_msc_test_unit_ready_cb(0));CHECK(key==2 && asc==0x3a);
    tud_msc_capacity_cb(0,&blocks,&size);CHECK(!blocks && size==512);
    memset(data,0xab,sizeof(data));CHECK(tud_msc_read10_cb(0,0,0,data,512)<0);CHECK(zero(data,512));
    CHECK(tud_msc_write10_cb(0,0,0,data,512)<0);CHECK(!writes && zero(data,512));
    unlocked=true;++generation;
    CHECK(!tud_msc_test_unit_ready_cb(0));CHECK(key==6 && asc==0x28);
    CHECK(tud_msc_test_unit_ready_cb(0));
    tud_msc_capacity_cb(0,&blocks,&size);CHECK(blocks==16 && size==512);
    CHECK(tud_msc_is_writable_cb(0));
    memset(data,0x5a,sizeof(data));
    CHECK(tud_msc_write10_cb(0,8,0,data,sizeof(data))==sizeof(data));
    CHECK(writes==1 && zero(data,sizeof(data)) && zero(last_scratch,FV_USB_IO_BYTES));
    CHECK(tud_msc_read10_cb(0,7,512,data,sizeof(data))==sizeof(data));
    CHECK(last_lba==8 && last_count==8 && data[0]==0x5a && data[4095]==0x5a);
    CHECK(zero(last_scratch,FV_USB_IO_BYTES));
    CHECK(tud_msc_read10_cb(0,15,0,data,512)==512);
    CHECK(tud_msc_read10_cb(0,16,0,data,512)<0 && asc==0x21);
    CHECK(tud_msc_read10_cb(0,15,0,data,1024)<0 && asc==0x21);
    uint32_t previous=calls;
    CHECK(tud_msc_read10_cb(0,UINT32_MAX,512,data,512)<0);
    CHECK(tud_msc_read10_cb(0,0,1,data,512)<0);
    CHECK(tud_msc_read10_cb(0,0,0,data,511)<0);
    CHECK(tud_msc_read10_cb(0,0,0,data,4097)<0);
    CHECK(tud_msc_read10_cb(1,0,0,data,512)<0);
    CHECK(calls==previous);
    failure=FV_BLOCK_ERROR_INTEGRITY;memset(data,0xab,512);
    CHECK(tud_msc_read10_cb(0,0,0,data,512)<0 && key==3 && asc==0x11);
    CHECK(zero(data,512) && zero(last_scratch,FV_USB_IO_BYTES));
    failure=FV_BLOCK_ERROR_IO;memset(data,0xab,512);
    CHECK(tud_msc_write10_cb(0,0,0,data,512)<0 && asc==0x0c);
    CHECK(zero(data,512) && zero(last_scratch,FV_USB_IO_BYTES));failure=0;
    uint8_t command[16]={0x35};CHECK(tud_msc_scsi_cb(0,command,NULL,0)==0);
    command[0]=0xff;CHECK(tud_msc_scsi_cb(0,command,NULL,0)<0 && asc==0x20);
    CHECK(tud_msc_start_stop_cb(0,0,false,true));CHECK(!unlocked);
    CHECK(!tud_msc_test_unit_ready_cb(0));CHECK(!tud_msc_is_writable_cb(0));
    command[0]=0x35;CHECK(tud_msc_scsi_cb(0,command,NULL,0)<0);
    CHECK(!tud_msc_start_stop_cb(0,0,true,true));CHECK(!unlocked);
    puts("USB storage callback checks passed");return 0;
}
