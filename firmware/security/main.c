#include "bringup.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"
queue_t commands,responses;
/* Explicit stack for envelope/DRBG/session scratch. Static sessions/bulk buffers
 * live outside it; stack usage artifacts are emitted for board bring-up review. */
static uint32_t worker_stack[8192] __attribute__((aligned(8)));
int main(void) {
    gpio_init(1);gpio_put(1,1);gpio_set_dir(1,GPIO_OUT);
    gpio_init(0);gpio_put(0,0);gpio_set_dir(0,GPIO_OUT);
    const unsigned inputs[]={2,3,16,17};
    for(unsigned i=0;i<4;i++){gpio_init(inputs[i]);gpio_set_dir(inputs[i],GPIO_IN);gpio_disable_pulls(inputs[i]);}
    gpio_init(18);gpio_put(18,1);gpio_set_dir(18,GPIO_OUT);
    if(!flash_safe_execute_core_init())panic("flash lockout init");
    queue_init(&commands,sizeof(fv_command),1);queue_init(&responses,sizeof(uint32_t),1);
    multicore_launch_core1_with_stack(security_worker,worker_stack,sizeof(worker_stack));
    tusb_init();
    for(;;){gpio_put(1,gpio_get(2)||gpio_get(16));tud_task();security_usb_poll();}
}
