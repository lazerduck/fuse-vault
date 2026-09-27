#include "bringup.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"
#if FV_USB_FIDO
#include "fido_adapter.h"
#endif
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
queue_t ui_responses;
#endif
void fv_usb_mux_poll(void);
queue_t commands,responses;
#if FV_USB_MSC
queue_t storage_responses;
#endif
/* Explicit stack for envelope/DRBG/session scratch. Static sessions/bulk buffers
 * live outside it; stack usage artifacts are emitted for board bring-up review. */
static uint32_t worker_stack[FV_USB_FIDO?16384:8192] __attribute__((aligned(8)));
#if FV_DEBUG_STARTUP
volatile uint32_t startup_stage;
volatile bool startup_requested;
#endif
int main(void) {
    boot_trace_mark(3);
    gpio_init(1);gpio_put(1,1);gpio_set_dir(1,GPIO_OUT);
    gpio_init(0);gpio_put(0,0);gpio_set_dir(0,GPIO_OUT);
    const unsigned inputs[]={2,3,16,17};
    for(unsigned i=0;i<4;i++){gpio_init(inputs[i]);gpio_set_dir(inputs[i],GPIO_IN);gpio_disable_pulls(inputs[i]);}
    gpio_init(18);gpio_put(18,1);gpio_set_dir(18,GPIO_OUT);
#if FV_DEVICE_UI
    queue_init(&ui_responses,sizeof(fv_ui_result),1);
    fv_device_ui_init();
#endif
    queue_init(&commands,sizeof(fv_command),1);queue_init(&responses,sizeof(uint32_t),1);
#if FV_USB_MSC
    queue_init(&storage_responses,sizeof(fv_usb_response),1);
#endif
#if !FV_DEBUG_STARTUP
    if(!flash_safe_execute_core_init())panic("flash lockout init");
    multicore_launch_core1_with_stack(security_worker,worker_stack,sizeof(worker_stack));
#endif
    boot_trace_mark(4);
    tusb_init();
    boot_trace_mark(5);
    for(;;){
        fv_usb_mux_poll();tud_task();
#if FV_USB_FIDO
        fv_fido_poll();
#endif
#if FV_USB_MSC
        fv_usb_storage_poll();
#endif
        security_usb_poll();
#if FV_DEVICE_UI
        fv_device_ui_poll();
#endif
        boot_trace_poll();
#if FV_DEBUG_STARTUP
        static uint64_t start_at;
        if(startup_requested && !startup_stage) {
            if(!start_at)start_at=time_us_64()+100000;
            if(time_us_64()>=start_at) {
                startup_stage=1;
                if(!flash_safe_execute_core_init())panic("flash lockout init");
                startup_stage=2;
                multicore_launch_core1_with_stack(security_worker,worker_stack,sizeof(worker_stack));
                /* Worker now owns stage values 10..14. */
            }
        }
#endif
    }
}
