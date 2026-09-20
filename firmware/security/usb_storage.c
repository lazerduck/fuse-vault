#include "usb_storage.h"
#include "tusb.h"
#include "fuse_vault/block_device.h"
#include <string.h>
/* Never put keys on core 0. This aligned buffer contains at most one I/O batch. */
static _Alignas(4) uint8_t scratch[FV_USB_IO_BYTES];
static uint32_t observed_generation;
static uint32_t activity_read,activity_write;
void fv_usb_storage_take_activity(uint32_t *read_bytes,uint32_t *write_bytes){
    *read_bytes=activity_read;*write_bytes=activity_write;
    activity_read=activity_write=0;
}
static void *transport_buffer;
static uint32_t transport_bytes;
static void wipe(void *buffer,uint32_t bytes) {
    volatile uint8_t *p=buffer;while(bytes--)*p++=0;
}
static bool command_active,command_write,pending,ready,final_write;
static uint32_t command_lba,command_left,pending_bytes;
_Static_assert(FV_USB_IO_BYTES>=512 && FV_USB_IO_BYTES<=32768 && FV_USB_IO_BYTES%512==0,
    "USB batch must fit the vault 64-sector batch limit");
static fv_usb_response completed;
static void clear_pipeline(void){
    /* Never wipe a buffer while core 1 is reading or writing it. */
    fv_usb_async_wait();
    fv_usb_response discarded;(void)fv_usb_async_take(&discarded);
    pending=ready=final_write=command_active=false;
    wipe(scratch,sizeof(scratch));
    if(transport_buffer)wipe(transport_buffer,transport_bytes);
}
void fv_usb_storage_clear_transport(void) {
    clear_pipeline();activity_read=activity_write=0;
}
static bool harvest(void){
    if(pending && fv_usb_async_take(&completed)){
        pending=false;ready=true;
        if(command_write && completed.result==FV_BLOCK_OK && completed.unlocked)activity_write+=pending_bytes;
    }
    return ready;
}
static bool start_batch(bool write,uint32_t bytes){
    if(!fv_usb_async_submit((fv_usb_request){.op=write?FV_USB_WRITE:FV_USB_READ,
            .lba=command_lba,.count=bytes/512,.data=scratch}))return false;
    pending_bytes=bytes;pending=true;ready=false;return true;
}
static bool valid_lun(uint8_t lun) {
    if(!lun)return true;
    tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x25,0);return false;
}
static bool available(uint8_t lun,fv_usb_response r) {
    if(!r.unlocked || !r.blocks) {
        tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);return false;
    }
    return true;
}
static int32_t io_error(uint8_t lun,int result,bool write) {
    switch(result) {
    case FV_BLOCK_ERROR_NOT_READY:tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);break;
    case FV_BLOCK_ERROR_OUT_OF_RANGE:tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x21,0);break;
    case FV_BLOCK_ERROR_INVALID_ARGUMENT:tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x24,0);break;
    default:tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,write?0x0c:0x11,0);break;
    }
    return -1;
}
void tud_msc_inquiry_cb(uint8_t lun,uint8_t vendor[8],uint8_t product[16],uint8_t revision[4]) {
    (void)lun;memcpy(vendor,"FuseVlt ",8);memcpy(product,"Encrypted Vault ",16);memcpy(revision,"0001",4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if(!valid_lun(lun))return false;
    fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});
    if(!available(lun,r)){observed_generation=r.generation;return false;}
    if(observed_generation!=r.generation) {
        observed_generation=r.generation;
        tud_msc_set_sense(lun,SCSI_SENSE_UNIT_ATTENTION,0x28,0);return false;
    }
    return true;
}
void tud_msc_capacity_cb(uint8_t lun,uint32_t *blocks,uint16_t *size) {
    *blocks=0;*size=512;if(!valid_lun(lun))return;
    fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});
    if(available(lun,r))*blocks=r.blocks;
}
bool fv_usb_storage_begin(uint8_t lun,bool write,uint32_t lba,uint32_t bytes,uint16_t block_size){
    clear_pipeline();
    if(!valid_lun(lun))return false;
    if(block_size!=512 || !bytes || bytes%512){io_error(lun,FV_BLOCK_ERROR_INVALID_ARGUMENT,write);return false;}
    fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});
    if(!available(lun,r))return false;
    if(lba>=r.blocks || bytes/512>r.blocks-lba){io_error(lun,FV_BLOCK_ERROR_OUT_OF_RANGE,write);return false;}
    command_active=true;command_write=write;command_lba=lba;command_left=bytes;
    return true;
}
bool tud_msc_is_writable_cb(uint8_t lun) {
    /* Called before EVERY USB receive: do not wait for the previous SD write. */
    if(command_active && command_write)return valid_lun(lun);
    return valid_lun(lun) && available(lun,fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS}));
}
static int32_t transfer(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t bytes,bool write) {
    if(!valid_lun(lun))return -1;
    if(!buffer || !bytes || bytes>sizeof(scratch) || bytes%512 || offset%512)
        return io_error(lun,FV_BLOCK_ERROR_INVALID_ARGUMENT,write);
    if(transport_buffer!=buffer){transport_buffer=buffer;transport_bytes=bytes;}
    else if(bytes>transport_bytes)transport_bytes=bytes;
    fv_usb_storage_poll(); /* Suppress publication following reset/disconnect. */
    if(!command_active || command_write!=write){wipe(buffer,bytes);return io_error(lun,FV_BLOCK_ERROR_NOT_READY,write);}
    if(lba>UINT32_MAX-offset/512 || lba+offset/512!=command_lba || bytes>command_left)
        return io_error(lun,FV_BLOCK_ERROR_INVALID_ARGUMENT,write);
    if(pending && !harvest())return 0; /* TinyUSB retries; USB IRQs keep running. */
    if(ready && (completed.result!=FV_BLOCK_OK || !completed.unlocked)){
        int result=completed.result?completed.result:FV_BLOCK_ERROR_NOT_READY;
        clear_pipeline();return io_error(lun,result,write);
    }
    if(write){
        if(final_write){
            /* Do not let TinyUSB emit a successful CSW before this completes. */
            final_write=false;ready=false;command_active=false;
            wipe(scratch,sizeof(scratch));wipe(buffer,bytes);
            return (int32_t)bytes;
        }
        if(ready){ready=false;wipe(scratch,sizeof(scratch));}
        memcpy(scratch,buffer,bytes);
        if(!start_batch(true,bytes)){wipe(scratch,sizeof(scratch));return 0;}
        if(bytes==command_left){final_write=true;return 0;}
        /* USB may now fill its endpoint buffer while core 1 owns scratch. */
        command_left-=bytes;command_lba+=bytes/512;
        wipe(buffer,bytes);return (int32_t)bytes;
    }
    if(!ready){
        memset(buffer,0,bytes);
        (void)start_batch(false,bytes);return 0;
    }
    memcpy(buffer,scratch,bytes);wipe(scratch,sizeof(scratch));ready=false;
    command_left-=bytes;command_lba+=bytes/512;activity_read+=bytes;
    if(command_left){
        /* Prefetch only within the validated host command, never beyond it. */
        uint32_t next=command_left<sizeof(scratch)?command_left:sizeof(scratch);
        (void)start_batch(false,next);
    }else command_active=false;
    return (int32_t)bytes;
}
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t bytes) {
    return transfer(lun,lba,offset,buffer,bytes,false);
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buffer,uint32_t bytes) {
    return transfer(lun,lba,offset,buffer,bytes,true);
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t power,bool start,bool eject) {
    (void)power;if(!valid_lun(lun))return false;
    if(eject && !start) {
        fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_LOCK});
        fv_usb_storage_clear_transport();
        return r.result==0;
    }
    return available(lun,fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS}));
}
int32_t tud_msc_scsi_cb(uint8_t lun,const uint8_t command[16],void *buffer,uint16_t bytes) {
    (void)buffer;(void)bytes;if(!valid_lun(lun))return -1;
    /* Every write already syncs both ciphertext and metadata before returning. */
    if(command[0]==0x35) {
        fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_SYNC});
        return r.result==FV_BLOCK_OK?0:io_error(lun,r.result,true);
    }
    tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0);return -1;
}
