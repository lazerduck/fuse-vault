#include "fuse_vault/tft_display.h"
#include "device_ui.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <string.h>

/* N096-1608TBBIG09-C08: ST7735S, four-wire SPI, 80x160.
 * Datasheet supplies no register recipe. Retain V1's candidate landscape
 * mapping until a physical panel confirms offsets and inversion. */
#define WIDTH 160u
#define HEIGHT 80u
_Static_assert(FV_SCREEN_BYTES == WIDTH * HEIGHT / 8, "TFT framebuffer size");
static uint8_t sent[FV_SCREEN_BYTES];
static unsigned row;
static bool first;
static bool backlight;
static uint64_t next_frame;

static void command(uint8_t cmd, const uint8_t *data, size_t size) {
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, 0);
    gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, 0);
    spi_write_blocking(spi0, &cmd, 1);
    if (size) {
        gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, 1);
        spi_write_blocking(spi0, data, size);
    }
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, 1);
}

void fv_tft_init(void) {
    const unsigned pins[] = {FUSE_VAULT_TFT_BACKLIGHT_PIN,
        FUSE_VAULT_TFT_RESET_PIN, FUSE_VAULT_TFT_DATA_COMMAND_PIN,
        FUSE_VAULT_TFT_CHIP_SELECT_PIN};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_put(pins[i], pins[i] == FUSE_VAULT_TFT_BACKLIGHT_PIN ?
                 !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL : 1);
        gpio_set_dir(pins[i], GPIO_OUT);
    }
    gpio_set_function(FUSE_VAULT_TFT_CLOCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FUSE_VAULT_TFT_MOSI_PIN, GPIO_FUNC_SPI);
    spi_init(spi0, 8000000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(FUSE_VAULT_TFT_RESET_PIN, 0);
    sleep_ms(20);
    gpio_put(FUSE_VAULT_TFT_RESET_PIN, 1);
    sleep_ms(120);
    command(0x01, NULL, 0); /* software reset */
    sleep_ms(150);
    command(0x11, NULL, 0); /* sleep out */
    sleep_ms(120);
    const uint8_t rgb565 = 0x05, landscape = 0x60;
    command(0x3a, &rgb565, 1);
    command(0x36, &landscape, 1);
    command(0x21, NULL, 0); /* inversion on */
    command(0x13, NULL, 0); /* normal mode */
    sleep_ms(10);
    command(0x29, NULL, 0); /* display on */
    sleep_ms(100);
    memset(sent, 0, sizeof(sent));
    row = 0; first = true; backlight = false; next_frame = 0;
}

void fv_tft_poll(const uint8_t *framebuffer) {
    if (time_us_64() < next_frame) return;
    const uint8_t *source = framebuffer + row * (WIDTH/8);
    uint8_t *previous = sent + row * (WIDTH/8);
    if (first || memcmp(source, previous, WIDTH/8)) {
        /* Single-row transactions leave CS idle between polls. At 8 MHz
         * each update occupies about 331 us, rather than 26 ms per frame.
         * Read the current UI row so cancelled secrets aren't queued. */
        uint8_t pixels[WIDTH * 2];
        for (unsigned x = 0; x < WIDTH; ++x) {
            uint8_t value = (source[x/8] & (0x80u >> (x%8))) ? 0xff : 0;
            pixels[2*x] = value; pixels[2*x+1] = value;
        }
        const uint8_t columns[] = {0, 1, 0, WIDTH};
        const uint8_t rows[] = {0, (uint8_t)(26+row), 0, (uint8_t)(26+row)};
        command(0x2a, columns, sizeof(columns));
        command(0x2b, rows, sizeof(rows));
        command(0x2c, pixels, sizeof(pixels));
        memcpy(previous, source, WIDTH/8);
    }
    if (++row == HEIGHT) {
        row = 0; first = false;
        if (!backlight) {
            gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN, FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
            backlight = true;
        }
        next_frame = time_us_64() + 10000;
    }
}
