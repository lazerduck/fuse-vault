#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

void fv_usb_mux_poll(void);
static uint64_t now_us;
static bool a_present, c_present;
static char events[32];
static unsigned event_count;

static void record(char event) {
    CHECK(event_count + 1 < sizeof(events));
    events[event_count++] = event;
    events[event_count] = 0;
}
uint64_t time_us_64(void) {
    return now_us;
}
bool gpio_get(unsigned pin) {
    CHECK(pin == FUSE_VAULT_USB_A_PRESENT_PIN || pin == FUSE_VAULT_USB_C_PRESENT_PIN);
    return pin == FUSE_VAULT_USB_A_PRESENT_PIN ? a_present : c_present;
}
void gpio_put(unsigned pin, bool value) {
    if (pin == FUSE_VAULT_USB_OUTPUT_ENABLE_PIN)
        record(value == FUSE_VAULT_USB_MUX_ENABLE_LEVEL ? 'E' : 'X');
    else {
        CHECK(pin == FUSE_VAULT_USB_SELECT_PIN);
        record(value == FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL ? 'C' : 'A');
    }
}
void tud_disconnect(void) {
    record('D');
}
void tud_connect(void) {
    record('R');
}
void tud_umount_cb(void) {
    record('S');
}
void fv_device_ui_disconnect(void) {
    record('U');
}

static void poll(uint64_t time, bool a, bool c, const char *expected) {
    now_us = time;
    a_present = a;
    c_present = c;
    event_count = 0;
    events[0] = 0;
    fv_usb_mux_poll();
    CHECK(!strcmp(events, expected));
}

int main(void) {
    poll(0, false, false, "");
    poll(1000, true, false, "");
    poll(2000, false, false, ""); /* A bouncing cable must not switch the mux. */
    poll(3000, true, false, "");
    poll(27999, true, false, "");
    poll(28000, true, false, "DXASU"); /* Disconnect, disable, select A, notify. */
    poll(52999, true, false, "");
    poll(53000, true, false, "ER"); /* Enable before reconnecting USB. */
    poll(60000, true, false, "");

    poll(61000, true, true, "");
    poll(86000, true, true, "DXCSU"); /* C wins when both are present. */
    poll(111000, true, true, "ER");
    poll(112000, false, true, ""); /* Removing A cannot disturb C. */
    poll(140000, false, true, "");

    poll(141000, true, false, "");
    poll(166000, true, false, "DXASU"); /* Fall back to A when C disappears. */
    poll(191000, true, false, "ER");
    poll(192000, false, false, "");
    poll(217000, false, false, "DXCSU");
    poll(242000, false, false, ""); /* No connector: remain disconnected. */
    poll(300000, false, false, "");

    /* A new stable route supersedes a reconnect that has not happened yet. */
    poll(301000, true, false, "");
    poll(326000, true, false, "DXASU");
    poll(326000, true, true, "");
    poll(351000, true, true, "DXCSU");
    poll(376000, true, true, "ER");
    puts("USB mux priority, debounce, switch ordering and disconnect checks passed");
    return 0;
}
