#include "fuse_vault/rp2354_connector.h"

#include "pico/stdlib.h"

#include <stddef.h>

#if FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED
#if !defined(FUSE_VAULT_USB_MUX_ENABLE_LEVEL) || \
    !defined(FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL)
#error "Confirmed USB mux builds must define OE and select levels"
#endif

_Static_assert(FUSE_VAULT_USB_MUX_ENABLE_LEVEL == 0 ||
                   FUSE_VAULT_USB_MUX_ENABLE_LEVEL == 1,
               "USB mux enable level must be 0 or 1");
_Static_assert(FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL == 0 ||
                   FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL == 1,
               "USB mux select level must be 0 or 1");
#endif

#if FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
#if !defined(FUSE_VAULT_USB_A_PRESENT_LEVEL) || \
    !defined(FUSE_VAULT_USB_C_PRESENT_LEVEL)
#error "Confirmed USB presence builds must define both active levels"
#endif
_Static_assert(FUSE_VAULT_USB_A_PRESENT_LEVEL == 0 ||
                   FUSE_VAULT_USB_A_PRESENT_LEVEL == 1,
               "USB-A presence level must be 0 or 1");
_Static_assert(FUSE_VAULT_USB_C_PRESENT_LEVEL == 0 ||
                   FUSE_VAULT_USB_C_PRESENT_LEVEL == 1,
               "USB-C presence level must be 0 or 1");
#endif

static bool disable_mux(void *context) {
    fv_rp2354_connector_t *connector = context;
    if (connector == NULL) return false;
#if !FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED
    return false;
#else
    gpio_init(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN);
    gpio_put(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN,
             !FUSE_VAULT_USB_MUX_ENABLE_LEVEL);
    gpio_set_dir(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN, GPIO_OUT);
    return true;
#endif
}

static bool configure_presence_inputs(void *context) {
    fv_rp2354_connector_t *connector = context;
    if (connector == NULL) return false;
    const uint8_t inputs[] = {
        FUSE_VAULT_USB_A_PRESENT_PIN,
        FUSE_VAULT_USB_C_PRESENT_PIN,
        FUSE_VAULT_USB_A_PRESENT_DUPLICATE_PIN,
        FUSE_VAULT_USB_C_PRESENT_DUPLICATE_PIN,
    };
    for (size_t index = 0u; index < sizeof(inputs); ++index) {
        gpio_init(inputs[index]);
        gpio_set_dir(inputs[index], GPIO_IN);
        gpio_disable_pulls(inputs[index]);
    }
    connector->pins_configured = true;
    return true;
}

#if FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
static bool read_pair(uint8_t canonical_pin, uint8_t duplicate_pin,
                      bool active_level, bool *present) {
    if (present == NULL) return false;
    const bool canonical = gpio_get(canonical_pin);
    const bool duplicate = gpio_get(duplicate_pin);
    if (canonical != duplicate) return false;
    *present = canonical == active_level;
    return true;
}
#endif

static bool read_usb_a_present(void *context, bool *present) {
    const fv_rp2354_connector_t *connector = context;
    if (connector == NULL || !connector->pins_configured) return false;
#if !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
    (void)present;
    return false;
#else
    return read_pair(FUSE_VAULT_USB_A_PRESENT_PIN,
                     FUSE_VAULT_USB_A_PRESENT_DUPLICATE_PIN,
                     FUSE_VAULT_USB_A_PRESENT_LEVEL, present);
#endif
}

static bool read_usb_c_present(void *context, bool *present) {
    const fv_rp2354_connector_t *connector = context;
    if (connector == NULL || !connector->pins_configured) return false;
#if !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
    (void)present;
    return false;
#else
    return read_pair(FUSE_VAULT_USB_C_PRESENT_PIN,
                     FUSE_VAULT_USB_C_PRESENT_DUPLICATE_PIN,
                     FUSE_VAULT_USB_C_PRESENT_LEVEL, present);
#endif
}

static bool route_connector(void *context, fv_connector_state_t route) {
    fv_rp2354_connector_t *connector = context;
    if (connector == NULL || !connector->pins_configured ||
        (route != FV_CONNECTOR_USB_A && route != FV_CONNECTOR_USB_C)) {
        return false;
    }
#if !FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED || \
    !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
    return false;
#else
    const bool select_c = FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL;
    gpio_init(FUSE_VAULT_USB_SELECT_PIN);
    gpio_put(FUSE_VAULT_USB_SELECT_PIN,
             route == FV_CONNECTOR_USB_C ? select_c : !select_c);
    gpio_set_dir(FUSE_VAULT_USB_SELECT_PIN, GPIO_OUT);
    gpio_put(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN,
             FUSE_VAULT_USB_MUX_ENABLE_LEVEL);
    return true;
#endif
}

const fv_connector_ops_t fv_rp2354_connector_ops = {
    .disable_mux = disable_mux,
    .configure_presence_inputs = configure_presence_inputs,
    .read_usb_a_present = read_usb_a_present,
    .read_usb_c_present = read_usb_c_present,
    .route_connector = route_connector,
};

void fv_rp2354_connector_init(fv_rp2354_connector_t *connector) {
    if (connector != NULL) connector->pins_configured = false;
}
