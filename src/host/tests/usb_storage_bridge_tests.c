#include "bringup.h"
#include "device/dcd.h"
#include "fuse_vault/block_device.h"
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
atomic_bool fv_ui_maintenance;
static unsigned disconnects;
void fv_device_ui_refresh(void){}
void fv_device_ui_media_changed(bool unlocked){(void)unlocked;}
void fv_device_ui_disconnect(void){++disconnects;}
#endif
#if FV_USB_FIDO
#include "fido_adapter.h"
static bool fido_busy,fido_disk;
static unsigned fido_cancels;
bool fv_fido_busy(void){return fido_busy;}
bool fv_fido_disk_available(void){return fido_disk;}
void fv_fido_disconnect(void){++fido_cancels;}
#endif
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void tud_event_hook_cb(uint8_t,uint32_t,bool);
void tud_umount_cb(void);
void tud_suspend_cb(bool);
queue_t commands,responses,storage_responses;
static fv_usb_request request;
static unsigned locks,reads;
static uint32_t generation;
void fv_usb_storage_clear_transport(void){fv_usb_async_wait();fv_usb_response r;(void)fv_usb_async_take(&r);fv_usb_forget_status();}
static bool open=true,reset_during_read;
static fv_access_mode mode=FV_ACCESS_FULL;
void queue_add_blocking(queue_t *q,const void *p){CHECK(q==&commands);request=((const fv_command*)p)->storage;}
void queue_remove_blocking(queue_t *q,void *p) {
    CHECK(q==&storage_responses);
    if(request.op==FV_USB_LOCK){++locks;open=false;fv_usb_publish_generation(++generation);}
    if(request.op==FV_USB_READ) {
        ++reads;
        if(reset_during_read){reset_during_read=false;tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);}
    }
    *(fv_usb_response*)p=(fv_usb_response){.unlocked=open,.blocks=open && fv_access_files(mode)?16:0,
        .mode=open?mode:FV_ACCESS_LOCKED,.generation=generation};
}
bool queue_try_add(queue_t *q,const void *p){queue_add_blocking(q,p);return true;}
bool queue_try_remove(queue_t *q,void *p){queue_remove_blocking(q,p);return true;}
int main(void) {
    fv_usb_response r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked);
    tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);
    CHECK(locks==0 && fv_usb_session_invalidated()); /* ISR must never block on a queue. */
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(!r.unlocked && locks==1);
    CHECK(!fv_usb_session_invalidated());
    open=true;tud_event_hook_cb(0,DCD_EVENT_UNPLUGGED,true);fv_usb_storage_poll();CHECK(!open && locks==2);
    open=true;tud_suspend_cb(false);fv_usb_storage_poll();CHECK(!open && locks==3);
    open=true;tud_umount_cb();fv_usb_storage_poll();CHECK(!open && locks==4);
    open=true;reset_during_read=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_READ,.generation=generation});
    CHECK(reads==1 && locks==5 && !r.unlocked && !r.blocks && r.result==FV_BLOCK_ERROR_NOT_READY);
    fv_usb_storage_poll();CHECK(locks==5);
#if FV_DEVICE_UI
    open=true;unsigned previous_locks=locks,previous_reads=reads;
    atomic_store(&fv_ui_maintenance,true);
    tud_event_hook_cb(0,DCD_EVENT_UNPLUGGED,true);
    fv_usb_storage_poll();CHECK(locks==previous_locks);
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_READ,.generation=generation});
    CHECK(!r.unlocked && r.result==FV_BLOCK_ERROR_NOT_READY && reads==previous_reads);
    CHECK(disconnects>=1); /* UI is notified even while worker is formatting. */
    atomic_store(&fv_ui_maintenance,false);fv_usb_storage_poll();
    CHECK(locks==previous_locks+1 && !open);
#endif
    open=true;
    CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    CHECK(!fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    CHECK(fv_usb_async_take(&r) && r.unlocked);
    CHECK(!fv_usb_async_take(&r));
    CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    fv_usb_async_wait();CHECK(fv_usb_async_take(&r));
    open=true;CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);
    CHECK(fv_usb_async_take(&r) && !r.unlocked && !r.blocks && r.mode==FV_ACCESS_LOCKED);
    fv_usb_storage_poll();
    open=true;CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);fv_usb_storage_poll();
    CHECK(!open && !fv_usb_async_take(&r)); /* No stale completion after invalidation. */
#if FV_USB_FIDO
    open=true;r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked);
    fido_busy=true;unsigned before_reads=reads,before_locks=locks;
    CHECK(!fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked && reads==before_reads);
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_SYNC});CHECK(r.unlocked && r.result==0);
    fido_disk=true;CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    CHECK(fv_usb_worker_idle());CHECK(fv_usb_async_take(&r) && r.unlocked && reads==before_reads+1);
    fido_disk=false;CHECK(!fv_usb_async_submit((fv_usb_request){.op=FV_USB_WRITE,.generation=generation}));
    unsigned cancels=fido_cancels;tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);
    CHECK(fido_cancels==cancels+1);fv_usb_storage_poll();CHECK(locks==before_locks);
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(!r.unlocked && r.result==FV_BLOCK_ERROR_NOT_READY);
    fido_busy=false;fv_usb_storage_poll();CHECK(locks==before_locks+1);
    /* A permission snapshot must not survive a local session transition. */
    open=true;mode=FV_ACCESS_FULL;r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.mode==FV_ACCESS_FULL);
    fv_usb_storage_clear_transport();fido_busy=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(!r.unlocked && !r.blocks && r.mode==FV_ACCESS_LOCKED);
    fido_busy=false;mode=FV_ACCESS_PASSKEYS_ONLY;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked && !r.blocks && r.mode==mode);
    fido_busy=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_SYNC});CHECK(r.result==FV_BLOCK_ERROR_NOT_READY && r.unlocked);
    fido_busy=false;mode=FV_ACCESS_FILES_READ_ONLY;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked && r.blocks && r.mode==mode);
    fido_busy=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.mode==FV_ACCESS_FILES_READ_ONLY && r.blocks);
    fido_busy=false;
    /* Worker FIDO cancellation revokes a previously ready storage response
     * before core 0 has observed the FIDO done flag or cleared transport. */
    mode=FV_ACCESS_FULL;open=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked);
    CHECK(fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation}));
    CHECK(fv_usb_worker_idle()); /* async_ready holds the old successful read. */
    fido_busy=true;fv_usb_publish_generation(++generation);open=false;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});
    CHECK(!r.unlocked && !r.blocks && r.mode==FV_ACCESS_LOCKED && r.result==FV_BLOCK_ERROR_NOT_READY);
    CHECK(fv_usb_async_take(&r) && !r.unlocked && !r.blocks && r.result==FV_BLOCK_ERROR_NOT_READY);
    fido_busy=false;
    CHECK(!fv_usb_async_submit((fv_usb_request){.op=FV_USB_READ,.generation=generation-1}));
    /* Explicit MSC eject must survive a FIDO request whose cancellation is
     * deliberately non-destructive to the existing read-only session. */
    open=true;mode=FV_ACCESS_FILES_READ_ONLY;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_STATUS});CHECK(r.unlocked);
    before_locks=locks;cancels=fido_cancels;fido_busy=true;
    r=fv_usb_rpc((fv_usb_request){.op=FV_USB_LOCK});
    CHECK(r.result==FV_BLOCK_ERROR_NOT_READY && fv_usb_session_invalidated());
    CHECK(fido_cancels==cancels+1 && locks==before_locks && open);
    fv_usb_storage_poll();CHECK(locks==before_locks);
    fido_busy=false;fv_usb_storage_poll();
    CHECK(locks==before_locks+1 && !open && !fv_usb_session_invalidated());
#endif
    puts("USB reset/suspend invalidation checks passed");return 0;
}
