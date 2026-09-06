#include "pico/unique_id.h"
#include "tusb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef FUSE_VAULT_USB_VID
/* TinyUSB development VID. A production-assigned VID/PID is a release gate. */
#define FUSE_VAULT_USB_VID 0xcafeu
#endif
#ifndef FUSE_VAULT_USB_PID
#define FUSE_VAULT_USB_PID 0x4011u
#endif

static const tusb_desc_device_t DEVICE = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = FUSE_VAULT_USB_VID,
    .idProduct = FUSE_VAULT_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&DEVICE;
}

enum { INTERFACE_MSC, INTERFACE_COUNT };
#define CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
static const uint8_t CONFIGURATION[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIG_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(INTERFACE_MSC, 0, 0x01, 0x81, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return CONFIGURATION;
}

static uint16_t string_buffer[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    (void)language_id;
    const char *ascii = NULL;
    char serial[2u * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1u];
    size_t length = 0u;
    if (index == 0u) {
        string_buffer[1] = 0x0409u;
        length = 1u;
    } else {
        if (index == 1u) ascii = "Fuse Vault";
        else if (index == 2u) ascii = "Encrypted Storage";
        else if (index == 3u) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            ascii = serial;
        } else return NULL;
        length = strlen(ascii);
        if (length > 32u) length = 32u;
        for (size_t character = 0u; character < length; ++character) {
            string_buffer[character + 1u] = (uint16_t)(uint8_t)ascii[character];
        }
    }
    string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8u) |
                                  (uint16_t)(2u * length + 2u));
    return string_buffer;
}
