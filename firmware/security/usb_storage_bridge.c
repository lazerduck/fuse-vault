#include "bringup.h"
#include "tusb.h"
#include "fuse_vault/block_device.h"
#include "device/dcd.h"
#include <stdatomic.h>
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
#endif
/* ISR only marks invalidation. Vault operations always execute on core 1. */
static atomic_bool invalidate;
/* Core-0-owned queue state. The worker owns its buffer until its response. */
static bool async_pending,async_ready;
static fv_usb_response async_response;
void fv_usb_async_wait(void){
    if(async_pending){
        queue_remove_blocking(&storage_responses,&async_response);
        async_pending=false;async_ready=true;
    }
}
bool fv_usb_async_take(fv_usb_response *response){
    if(async_pending && queue_try_remove(&storage_responses,&async_response)){
        async_pending=false;async_ready=true;
    }
    if(!async_ready)return false;
    *response=async_response;async_ready=false;
#if FV_DEVICE_UI
    fv_device_ui_media_changed(response->unlocked);
#endif
    return true;
}
bool fv_usb_async_submit(fv_usb_request request){
#if FV_DEVICE_UI
    if(atomic_load(&fv_ui_maintenance))return false;
#endif
    if(atomic_load(&invalidate) || async_pending || async_ready)return false;
    fv_command command={0};command.storage=request;
    if(!queue_try_add(&commands,&command))return false;
    async_pending=true;return true;
}
static fv_usb_response transact(fv_usb_request request) {
    fv_usb_async_wait();
    fv_command command={0};command.storage=request;
    queue_add_blocking(&commands,&command);
    fv_usb_response response;queue_remove_blocking(&storage_responses,&response);
#if FV_DEVICE_UI
    fv_device_ui_media_changed(response.unlocked);
#endif
    return response;
}
void fv_usb_storage_poll(void) {
#if FV_DEVICE_UI
    if(atomic_load(&fv_ui_maintenance))return;
#endif
    if(atomic_exchange(&invalidate,false)) {
        (void)transact((fv_usb_request){.op=FV_USB_LOCK});
        fv_usb_storage_clear_transport();
#if FV_DEVICE_UI
        fv_device_ui_refresh();
#endif
    }
}
fv_usb_response fv_usb_rpc(fv_usb_request request) {
#if FV_DEVICE_UI
    if(atomic_load(&fv_ui_maintenance))return (fv_usb_response){.result=FV_BLOCK_ERROR_NOT_READY};
#endif
    fv_usb_storage_poll();
    fv_usb_response r=transact(request);
    /* Do not publish a completed read across a concurrent bus invalidation. */
    if(atomic_load(&invalidate)) {
        fv_usb_storage_poll();
        r.unlocked=false;r.blocks=0;r.result=FV_BLOCK_ERROR_NOT_READY;
    }
    return r;
}
void tud_event_hook_cb(uint8_t rhport,uint32_t eventid,bool in_isr) {
    (void)rhport;(void)in_isr;
    if(eventid==DCD_EVENT_BUS_RESET || eventid==DCD_EVENT_UNPLUGGED || eventid==DCD_EVENT_SUSPEND)
        {atomic_store(&invalidate,true);
#if FV_DEVICE_UI
        fv_device_ui_disconnect();
#endif
        }
}
void tud_umount_cb(void){atomic_store(&invalidate,true);
#if FV_DEVICE_UI
    fv_device_ui_disconnect();
#endif
}
void tud_suspend_cb(bool remote_wakeup_en){(void)remote_wakeup_en;tud_umount_cb();}
