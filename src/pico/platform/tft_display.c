#include "fuse_vault/tft_display.h"
#include "device_ui.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <string.h>
#include "startup.h"

/* N096-1608TBBIG09-C08: ST7735S, four-wire SPI, 80x160.
 * Datasheet supplies no register recipe. Landscape offsets are being
 * confirmed on hardware. Physical testing showed
 * inverted colours with inversion enabled, so use normal polarity. */
#define WIDTH 160u
#define HEIGHT 80u
/* Physical testing confirmed clean edges at X=0, Y=24. */
#define X_OFFSET 0u
#define Y_OFFSET 24u
_Static_assert(FV_SCREEN_BYTES == WIDTH * HEIGHT, "TFT framebuffer size");
static uint8_t sent[FV_SCREEN_BYTES];
static unsigned row;
static bool first;
static bool backlight;
static uint64_t next_frame;
static uint64_t startup_started;
static bool startup_active;
static unsigned startup_frame;
static uint64_t last_activity;
static bool sleeping;

bool fv_tft_activity(void) {
    uint64_t now = time_us_64();
    bool waking = sleeping || now - last_activity >= FV_TFT_IDLE_US;
    last_activity = now;
    sleeping = false;
    if (waking) {
        /* Refresh the whole current screen before lighting it again. */
        gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN, !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
        backlight = false;
        row = 0; first = true; next_frame = 0;
    }
    return waking;
}

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
    /* BGR panel order: RGB mode made the cyan accent appear yellow. */
    const uint8_t rgb565 = 0x05, landscape = 0x68;
    command(0x3a, &rgb565, 1);
    command(0x36, &landscape, 1);
    command(0x20, NULL, 0); /* inversion off: black UI background */
    command(0x13, NULL, 0); /* normal mode */
    sleep_ms(10);
    command(0x29, NULL, 0); /* display on */
    sleep_ms(100);
    memset(sent, 0, sizeof(sent));
    startup_active = false;
    row = 0; first = true; backlight = false; next_frame = 0;
    sleeping = false; last_activity = time_us_64();
}

void fv_tft_poll(const uint8_t *framebuffer) {
    if (time_us_64() - last_activity >= FV_TFT_IDLE_US) {
        sleeping = true;
        if (backlight) {
            gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN, !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
            backlight = false;
        }
    }
    if (sleeping) return;
    if (time_us_64() < next_frame) return;
    if (startup_active && row == 0) {
        uint64_t elapsed = time_us_64() - startup_started;
        if (elapsed >= 500000) startup_active = false;
        else startup_frame = (unsigned)(elapsed / 50000);
    }
    uint8_t decoded[WIDTH];
    const uint8_t *source;
    if (startup_active) {
        for (unsigned x = 0; x < WIDTH; ++x) {
            decoded[x] = fv_startup_pixel(startup_frame, row * WIDTH + x);
        }
        source = decoded;
    } else source = framebuffer + row * WIDTH;
    uint8_t *previous = sent + row * WIDTH;
    if (first || memcmp(source, previous, WIDTH)) {
        /* Single-row transactions leave CS idle between polls. At 8 MHz
         * each update occupies about 331 us, rather than 26 ms per frame.
         * Read the current UI row so cancelled secrets aren't queued. */
        uint8_t pixels[WIDTH * 2];
        for (unsigned x = 0; x < WIDTH; ++x) {
            unsigned r = source[x] >> 5, g = (source[x] >> 2) & 7, b = source[x] & 3;
            /* Replicate channel bits to span the full RGB565 range. */
            uint16_t value = (uint16_t)(((r << 2 | r >> 1) << 11) |
                                       ((g << 3 | g) << 5) | (b << 3 | b << 1 | b >> 1));
            pixels[2*x] = (uint8_t)(value >> 8); pixels[2*x+1] = (uint8_t)value;
        }
        const uint8_t columns[] = {0, X_OFFSET, 0, X_OFFSET + WIDTH - 1};
        const uint8_t rows[] = {0, (uint8_t)(Y_OFFSET+row), 0, (uint8_t)(Y_OFFSET+row)};
        command(0x2a, columns, sizeof(columns));
        command(0x2b, rows, sizeof(rows));
        command(0x2c, pixels, sizeof(pixels));
        memcpy(previous, source, WIDTH);
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

void fv_tft_show_startup(void) {
    startup_active = true;
    startup_started = time_us_64();
    startup_frame = 0;
    /* Decode and send the initial frame before turning on the backlight. */
    for (unsigned y = 0; y < HEIGHT; ++y)
        fv_tft_poll(NULL);
    startup_started = time_us_64();
}
