#include "bench.h"
#include "fuse_vault/rp2354_sd.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
static fv_rp2354_sd_t sd;
static bool initialized;
/* Interface must be valid even before the first INIT; no pins touched here. */
static uint64_t no_capacity(const fv_block_device_t *d) {(void)d;return 0;}
static const fv_block_device_ops_t unopened={.block_count=no_capacity};
static bool open_sd(void *unused) {
    (void)unused;
    bool ok=initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);
    initialized=true;
    return ok && sd.initialized;
}
void bench_worker(void) {
    sd.interface.ops=&unopened;
    fv_bench_engine engine={.device=&sd.interface,.open=open_sd,.now_us=time_us_64,
        .cpu_hz=clock_get_hz(clk_sys),.sd_hz=FV_SD_CLOCK_HZ};
    for(;;) {
        fv_bench_request request;
        fv_bench_response response;
        queue_remove_blocking(&commands,&request);
        /* Internal reset is queued by the USB owner after a disconnect. */
        if(request.op==0) {
            fv_bench_reset(&engine);
            response=(fv_bench_response){0};
        } else fv_bench_execute(&engine,&request,bench_buffer,&response);
        queue_add_blocking(&responses,&response);
    }
}
