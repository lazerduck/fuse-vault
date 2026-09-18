#include "fuse_vault/rp2354_connector.h"
#include "pico/stdlib.h"
#include <assert.h>
#include <stdio.h>
static unsigned pattern;
static bool enabled;
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_dir(unsigned pin, bool output) { (void)pin; (void)output; }
void gpio_disable_pulls(unsigned pin) { (void)pin; }
void gpio_put(unsigned pin, bool value) {
    if (pin == FUSE_VAULT_USB_OUTPUT_ENABLE_PIN)
        enabled = value == FUSE_VAULT_USB_MUX_ENABLE_LEVEL;
    else {
        assert(pin == FUSE_VAULT_USB_SELECT_PIN && !enabled);
        assert(value == FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL);
    }
}
bool gpio_get(unsigned pin) {
    unsigned bit;
    switch (pin) {
        case FUSE_VAULT_USB_A_PRESENT_PIN: bit=0; break;
        case FUSE_VAULT_USB_C_PRESENT_PIN: bit=1; break;
        case FUSE_VAULT_USB_A_PRESENT_DUPLICATE_PIN: bit=2; break;
        case FUSE_VAULT_USB_C_PRESENT_DUPLICATE_PIN: bit=3; break;
        default: assert(false); return false;
    }
    return (pattern & (1u << bit)) != 0;
}
int main(void) {
    for (unsigned p=0; p<16; ++p) {
        fv_rp2354_connector_t context;
        fv_connector_safety_t guard;
        pattern=10; /* USB-C only, both corresponding pins high. */
        fv_rp2354_connector_init(&context);
        assert(fv_connector_safety_init(&guard, &fv_rp2354_connector_ops,
                                       &context, NULL, NULL));
        assert(fv_connector_safety_route(&guard) && enabled);
        pattern=p;
        bool ok=fv_connector_safety_poll(&guard);
#if FUSE_VAULT_BENCH_FIXED_USB_C
        assert(ok && enabled && !guard.faulted);
        uint32_t stats[20];
        fv_rp2354_connector_diagnostic_snapshot(stats);
        assert(stats[2]==p && stats[3]==1 && stats[4+p]>0);
#else
        assert(ok == (p==10));
        assert(enabled == (p==10));
        if (p!=10) {
            pattern=10;
            assert(!fv_connector_safety_poll(&guard)); /* Existing latch preserved. */
        }
#endif
    }
    puts("All 16 presence patterns checked");
}
