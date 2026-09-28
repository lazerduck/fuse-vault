#include "pico/stdlib.h"
#include "tusb.h"
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
#endif

#if FV_USB_MSC
void tud_umount_cb(void);
#endif

#define USB_PRESENCE_DEBOUNCE_US 25000u
#define USB_RECONNECT_DELAY_US 25000u

/* Power detection state, not a third position of the binary select pin. */
typedef enum {
    USB_NO_POWER,
    USB_PORT_C,
    USB_PORT_A,
} usb_port;

static usb_port preferred_port(void) {
    if (gpio_get(FUSE_VAULT_USB_C_PRESENT_PIN) == FUSE_VAULT_USB_C_PRESENT_LEVEL)
        return USB_PORT_C;
    if (gpio_get(FUSE_VAULT_USB_A_PRESENT_PIN) == FUSE_VAULT_USB_A_PRESENT_LEVEL)
        return USB_PORT_A;
    return USB_NO_POWER;
}

void fv_usb_mux_poll(void) {
    static usb_port selected_port = USB_NO_POWER;
    static usb_port candidate_port = USB_NO_POWER;
    static uint64_t candidate_since_us;
    static uint64_t reconnect_at_us;

    uint64_t now_us = time_us_64();
    usb_port sensed_port = preferred_port();

    /* Ignore presence changes until the preferred connector has been stable. */
    if (sensed_port != candidate_port) {
        candidate_port = sensed_port;
        candidate_since_us = now_us;
    }
    if (candidate_port != selected_port &&
        now_us - candidate_since_us >= USB_PRESENCE_DEBOUNCE_US) {
        /* Disconnect and disable the data path before changing connectors. */
        tud_disconnect();
        gpio_put(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN, !FUSE_VAULT_USB_MUX_ENABLE_LEVEL);
        selected_port = candidate_port;
        gpio_put(FUSE_VAULT_USB_SELECT_PIN, selected_port == USB_PORT_A
                                                ? !FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL
                                                : FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL);
        reconnect_at_us = now_us + USB_RECONNECT_DELAY_US;

        /* Switching connectors ends the current host session. */
#if FV_USB_MSC
        tud_umount_cb();
#endif
#if FV_DEVICE_UI
        fv_device_ui_disconnect();
#endif
    }

    /* Keep the data path disabled during the reconnect delay and when unplugged. */
    if (reconnect_at_us && now_us >= reconnect_at_us) {
        reconnect_at_us = 0;
        if (selected_port != USB_NO_POWER) {
            gpio_put(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN, FUSE_VAULT_USB_MUX_ENABLE_LEVEL);
            tud_connect();
        }
    }
}
