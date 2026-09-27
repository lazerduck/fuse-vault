#include "bringup.h"
#include "pico/runtime.h"
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/gpio.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/ticks.h"
#include "tusb.h"
/* Diagnostic only. Retain a single tagged stage in scratch0: observed ROM
 * activity changes other scratch registers, so do not expose them as our data.
 * A warm image load preserves the preceding stage before arming a new trace. */
uint32_t boot_trace_previous[4];
#define TRACE_MAGIC 0x46564200u
void boot_trace_mark(uint32_t stage) {watchdog_hw->scratch[0]=TRACE_MAGIC|(stage&255u);}
static void boot_trace_early(void) {
    uint32_t previous=watchdog_hw->scratch[0];
    if((previous&0xffffff00u)==TRACE_MAGIC) {
        boot_trace_previous[0]=0x46564254;
        boot_trace_previous[1]=previous&255u;
    }
    boot_trace_mark(1);
    if(rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL,5000,0,0))boot_trace_mark(254);
}
PICO_RUNTIME_INIT_FUNC_HW(boot_trace_early,"00090");
/* Lexical init-array ordering: after 00100, before 00101. */
static void boot_trace_resets(void){boot_trace_mark(7);}
PICO_RUNTIME_INIT_FUNC_HW(boot_trace_resets,"001005");
static void boot_trace_usb_power(void){boot_trace_mark(8);}
PICO_RUNTIME_INIT_FUNC_HW(boot_trace_usb_power,"00102");
static void boot_trace_clocks(void){boot_trace_mark(2);}
PICO_RUNTIME_INIT_FUNC_HW(boot_trace_clocks,"00510");
void boot_trace_poll(void) {
    static bool complete;
    if(complete)return;
    if(tud_mounted()) {watchdog_disable();boot_trace_mark(6);complete=true;}
}
/* Preserve SDK behavior; only mark entry/return of clock-init dependencies. */
void __real_xosc_init(void);
void __wrap_xosc_init(void) {
    boot_trace_mark(10);__real_xosc_init();boot_trace_mark(11);
}
void __real_pll_init(PLL pll,uint refdiv,uint vco,uint post1,uint post2);
void __wrap_pll_init(PLL pll,uint refdiv,uint vco,uint post1,uint post2) {
    uint32_t stage=pll==pll_sys?12:14;
    boot_trace_mark(stage);__real_pll_init(pll,refdiv,vco,post1,post2);boot_trace_mark(stage+1);
}
void __real_vreg_set_voltage(enum vreg_voltage voltage);
void __wrap_vreg_set_voltage(enum vreg_voltage voltage) {
    boot_trace_mark(16);__real_vreg_set_voltage(voltage);boot_trace_mark(17);
}
void __real_clock_configure_undivided(clock_handle_t clock,uint32_t src,uint32_t aux,uint32_t hz);
void __wrap_clock_configure_undivided(clock_handle_t clock,uint32_t src,uint32_t aux,uint32_t hz) {
    uint32_t stage=20+2*(uint32_t)clock;
    boot_trace_mark(stage);__real_clock_configure_undivided(clock,src,aux,hz);boot_trace_mark(stage+1);
}
void __real_tick_start(tick_gen_num_t tick,uint cycles);
void __wrap_tick_start(tick_gen_num_t tick,uint cycles) {
    uint32_t stage=60+2*(uint32_t)tick;
    boot_trace_mark(stage);__real_tick_start(tick,cycles);boot_trace_mark(stage+1);
}
