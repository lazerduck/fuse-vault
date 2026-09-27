#include "bench.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"
queue_t commands, responses;
static uint32_t worker_stack[4096] __attribute__((aligned(8)));
int main(void) {
    /* Disconnect mux first, keep duplicated sense nets high impedance. */
    gpio_init(1); gpio_put(1,1); gpio_set_dir(1,GPIO_OUT);
    gpio_init(0); gpio_put(0,0); gpio_set_dir(0,GPIO_OUT);
    const unsigned inputs[]={2,3,16,17};
    for (unsigned i=0;i<4;i++) { gpio_init(inputs[i]); gpio_set_dir(inputs[i],GPIO_IN); gpio_disable_pulls(inputs[i]); }
    /* Damaged display stays disabled. */
    gpio_init(18); gpio_put(18,1); gpio_set_dir(18,GPIO_OUT);
    queue_init(&commands,sizeof(fv_bench_request),1);
    queue_init(&responses,sizeof(fv_bench_response),1);
    multicore_launch_core1_with_stack(bench_worker,worker_stack,sizeof(worker_stack));
    tusb_init();
    /* Bench selects USB-C. Conflicting USB-A presence disables the mux. */
    while (true) {
        gpio_put(1,gpio_get(2)||gpio_get(16));
        tud_task();
        bench_usb_poll();
    }
}
