/* Bench-only framebuffer mirror. Never enabled in a release image. */
#include "fuse_vault/usb_debug_display.h"
#include "fuse_vault/ui.h"
#include "fuse_vault/storage_profile.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <string.h>

static fv_framebuffer_t latest;
static struct {
    uint32_t header[8];
    fv_framebuffer_t frame;
    uint32_t timings[FV_PERF_COUNT * 4];
} packet;
static size_t sent, packet_size;
static bool transmitting;
static unsigned boot_flags, boot_recovery;
static uint32_t sequence;
_Static_assert(sizeof(packet) == 32 + 160 * 80 * 2 + FV_PERF_COUNT * 16, "Wire packet layout");

static bool initialize(void *context) {
    (void)context;
    /* Backlight off; every other display signal stays high impedance.
     * The connector's VDD remains physically wired to 3V3: UNPLUG THE SCREEN. */
    gpio_init(FUSE_VAULT_TFT_BACKLIGHT_PIN);
    gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN, !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
    gpio_set_dir(FUSE_VAULT_TFT_BACKLIGHT_PIN, GPIO_OUT);
    const unsigned pins[] = {FUSE_VAULT_TFT_RESET_PIN,
        FUSE_VAULT_TFT_DATA_COMMAND_PIN, FUSE_VAULT_TFT_CHIP_SELECT_PIN,
        FUSE_VAULT_TFT_CLOCK_PIN, FUSE_VAULT_TFT_MOSI_PIN};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        gpio_init(pins[i]); gpio_set_dir(pins[i], GPIO_IN); gpio_disable_pulls(pins[i]);
    }
    memset(&latest, 0, sizeof(latest));
    return true;
}
static bool present(void *context, const fv_ui_view_t *view) {
    (void)context;
    if (!view) return false;
    fv_ui_draw_view(view, &latest);
    ++sequence;
    return true; /* A missing viewer must never break the device runtime. */
}
const fv_display_ops_t fv_usb_debug_display_ops = {initialize, present};
void fv_usb_debug_set_boot(bool input, bool flash, bool roots, bool sd,
    bool usb, bool connector, bool services, unsigned recovery) {
    boot_flags = (unsigned)input | ((unsigned)flash << 1) | ((unsigned)roots << 2) |
        ((unsigned)sd << 3) | ((unsigned)usb << 4) | ((unsigned)connector << 5) |
        ((unsigned)services << 6);
    boot_recovery = recovery;
}
void fv_usb_debug_task(unsigned state, unsigned buttons) {
    if (!tud_cdc_connected()) {
        transmitting = false;
        sent = 0;
        tud_cdc_write_clear();
        return;
    }
    while (tud_cdc_available()) {
        int ch = tud_cdc_read_char();
        if ((ch != 'f' && ch != 'g') || transmitting) continue;
        packet.header[0] = 0x31445646u; /* FVD1, little endian */
        packet.header[1] = sizeof(packet.frame) + (ch == 'g' ? sizeof(packet.timings) : 0);
        packet_size = 32 + packet.header[1];
        fv_storage_profile_snapshot(packet.timings);
        packet.header[2] = sequence;
        packet.header[3] = state;
        packet.header[4] = buttons;
        packet.header[5] = boot_flags;
        packet.header[6] = boot_recovery;
        packet.header[7] = to_ms_since_boot(get_absolute_time());
        memcpy(&packet.frame, &latest, sizeof(latest));
        sent = 0;
        transmitting = true;
    }
    if (transmitting) {
        unsigned count = tud_cdc_write_available();
        if (count > packet_size - sent) count = packet_size - sent;
        if (count) sent += tud_cdc_write((const uint8_t *)&packet + sent, count);
        tud_cdc_write_flush();
        if (sent == packet_size) transmitting = false;
    }
}
