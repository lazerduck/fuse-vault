/* Compile the exact generated tud_task_ext body against a perpetual MSC retry.
 * The worker cannot finish until application UI gets time outside tud_task_ext.
 * A receive bound makes the original bug fail deterministically, not hang CI. */
#include "tusb.h"
#include "device/dcd.h"
#include "device/usbd_pvt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct {
    uint8_t speed,connected,remote_wakeup_en,sof_consumer;
    struct {bool busy,claimed;} ep_status[16][2];
    uint8_t ep2drv[16][2];
} _usbd_dev;
static unsigned _usbd_queued_setup;
#define SOF_CONSUMER_USER 0
#define CFG_TUD_LOG_LEVEL 2
#undef TU_LOG_USBD
#define TU_LOG_USBD(...) ((void)0)
#define _usbd_q NULL
static unsigned received,callbacks,ui_polls,cdc_polls,keepalive_polls;
static bool worker_done,pending=true;
bool tud_inited(void){return true;}
static bool receive_event(void *q,dcd_event_t *event,uint32_t timeout){
    (void)q;(void)timeout;
    if(++received>256){fputs("STARVED: USB never yielded to application polls\n",stderr);exit(42);}
    if(!pending)return false;
    pending=false;*event=(dcd_event_t){.event_id=DCD_EVENT_XFER_COMPLETE};
    event->xfer_complete.ep_addr=0x81;return true;
}
#define osal_queue_receive receive_event
static bool transfer(uint8_t port,uint8_t ep,xfer_result_t result,uint32_t bytes){
    (void)port;(void)ep;(void)result;(void)bytes;++callbacks;
    /* Same queue behavior as MSC's deferred read/write callbacks. */
    if(!worker_done)pending=true;
    return true;
}
static const usbd_class_driver_t driver={.xfer_cb=transfer};
static const usbd_class_driver_t *get_driver(uint8_t id){(void)id;return &driver;}
static void usbd_reset(uint8_t port){(void)port;}
static bool process_control_request(uint8_t port,const tusb_control_request_t *r){(void)port;(void)r;return true;}
void tud_umount_cb(void){}
void tud_suspend_cb(bool remote){(void)remote;}
void tud_resume_cb(void){}
void tud_sof_cb(uint32_t n){(void)n;}
void dcd_edpt_stall(uint8_t port,uint8_t ep){(void)port;(void)ep;}
bool usbd_control_xfer_cb(uint8_t port,uint8_t ep,xfer_result_t result,uint32_t bytes){(void)port;(void)ep;(void)result;(void)bytes;return true;}
#include FV_USB_TASK_BODY
int main(void){
    while(pending){
        unsigned before=callbacks;
        tud_task_ext(0,false);
        assert(callbacks-before<=8);
        ++ui_polls;++cdc_polls;++keepalive_polls;
        if(ui_polls==20)worker_done=true; /* User completes FIDO modal. */
    }
    assert(worker_done && ui_polls==21 && cdc_polls==21 && keepalive_polls==21);
    assert(callbacks==161);
    puts("Bounded USB retries allow UI, CDC and FIDO polls to progress");return 0;
}
