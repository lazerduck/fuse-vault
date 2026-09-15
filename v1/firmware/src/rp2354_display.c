#include "fuse_vault/rp2354_display.h"

#include "fuse_vault/ui.h"

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>

#define ST7735_SWRESET 0x01u
#define ST7735_SLPOUT 0x11u
#define ST7735_NORON 0x13u
#define ST7735_INVON 0x21u
#define ST7735_DISPON 0x29u
#define ST7735_CASET 0x2au
#define ST7735_RASET 0x2bu
#define ST7735_RAMWR 0x2cu
#define ST7735_MADCTL 0x36u
#define ST7735_COLMOD 0x3au

/* Candidate revision-1 landscape mapping for an 80x160 panel inside the
 * ST7735S 132x162 RAM. Enabled by default for bring-up; assembled-board
 * validation is recorded separately by DISPLAY_CONTROLLER_CONFIRMED. */
#define DISPLAY_MADCTL 0x60u
#define DISPLAY_X_OFFSET 1u
#define DISPLAY_Y_OFFSET 26u
#define DISPLAY_SPI_BAUD_HZ 8000000u

#if FUSE_VAULT_ENABLE_DISPLAY
static void write_command(uint8_t command, const uint8_t *parameters,
                          size_t parameter_count) {
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, false);
    gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, false);
    (void)spi_write_blocking(spi0, &command, 1u);
    if (parameter_count != 0u) {
        gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, true);
        (void)spi_write_blocking(spi0, parameters, parameter_count);
    }
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, true);
}

static void set_window(void) {
    const uint16_t x_start = DISPLAY_X_OFFSET;
    const uint16_t x_end = (uint16_t)(x_start + FV_DISPLAY_WIDTH - 1u);
    const uint16_t y_start = DISPLAY_Y_OFFSET;
    const uint16_t y_end = (uint16_t)(y_start + FV_DISPLAY_HEIGHT - 1u);
    const uint8_t columns[4] = {
        (uint8_t)(x_start >> 8u), (uint8_t)x_start,
        (uint8_t)(x_end >> 8u), (uint8_t)x_end,
    };
    const uint8_t rows[4] = {
        (uint8_t)(y_start >> 8u), (uint8_t)y_start,
        (uint8_t)(y_end >> 8u), (uint8_t)y_end,
    };
    write_command(ST7735_CASET, columns, sizeof(columns));
    write_command(ST7735_RASET, rows, sizeof(rows));
}
#endif

static bool initialize(void *context) {
    fv_rp2354_display_t *display = context;
    if (display == NULL) return false;
    display->initialized = false;

#if !FUSE_VAULT_ENABLE_DISPLAY
    return false;
#else
    gpio_init(FUSE_VAULT_TFT_BACKLIGHT_PIN);
    gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN,
             !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
    gpio_set_dir(FUSE_VAULT_TFT_BACKLIGHT_PIN, GPIO_OUT);
    gpio_init(FUSE_VAULT_TFT_RESET_PIN);
    gpio_set_dir(FUSE_VAULT_TFT_RESET_PIN, GPIO_OUT);
    gpio_init(FUSE_VAULT_TFT_DATA_COMMAND_PIN);
    gpio_set_dir(FUSE_VAULT_TFT_DATA_COMMAND_PIN, GPIO_OUT);
    gpio_init(FUSE_VAULT_TFT_CHIP_SELECT_PIN);
    gpio_set_dir(FUSE_VAULT_TFT_CHIP_SELECT_PIN, GPIO_OUT);
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, true);
    gpio_set_function(FUSE_VAULT_TFT_CLOCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FUSE_VAULT_TFT_MOSI_PIN, GPIO_FUNC_SPI);
    (void)spi_init(spi0, DISPLAY_SPI_BAUD_HZ);
    spi_set_format(spi0, 8u, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_put(FUSE_VAULT_TFT_RESET_PIN, false);
    sleep_ms(20u);
    gpio_put(FUSE_VAULT_TFT_RESET_PIN, true);
    sleep_ms(120u);
    write_command(ST7735_SWRESET, NULL, 0u);
    sleep_ms(150u);
    write_command(ST7735_SLPOUT, NULL, 0u);
    sleep_ms(120u);
    const uint8_t color_mode = 0x05u;
    write_command(ST7735_COLMOD, &color_mode, 1u);
    const uint8_t memory_control = DISPLAY_MADCTL;
    write_command(ST7735_MADCTL, &memory_control, 1u);
    write_command(ST7735_INVON, NULL, 0u);
    write_command(ST7735_NORON, NULL, 0u);
    sleep_ms(10u);
    write_command(ST7735_DISPON, NULL, 0u);
    sleep_ms(100u);
    gpio_put(FUSE_VAULT_TFT_BACKLIGHT_PIN,
             FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
    display->initialized = true;
    return true;
#endif
}

static bool present(void *context, const fv_ui_view_t *view) {
    fv_rp2354_display_t *display = context;
    if (display == NULL || view == NULL || !display->initialized) return false;
#if !FUSE_VAULT_ENABLE_DISPLAY
    return false;
#else
    /* The portable display boundary currently supplies a rendered view rather
     * than its source app. Render the same compact view locally to avoid a
     * second target-only UI model. */
    fv_framebuffer_t framebuffer;
    fv_ui_draw_view(view, &framebuffer);
    set_window();
    const uint8_t memory_write = ST7735_RAMWR;
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, false);
    gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, false);
    (void)spi_write_blocking(spi0, &memory_write, 1u);
    gpio_put(FUSE_VAULT_TFT_DATA_COMMAND_PIN, true);
    uint8_t line[FV_DISPLAY_WIDTH * 2u];
    for (unsigned y = 0u; y < FV_DISPLAY_HEIGHT; ++y) {
        for (unsigned x = 0u; x < FV_DISPLAY_WIDTH; ++x) {
            const fv_pixel_t pixel = framebuffer.pixels[y][x];
            line[x * 2u] = (uint8_t)(pixel >> 8u);
            line[x * 2u + 1u] = (uint8_t)pixel;
        }
        (void)spi_write_blocking(spi0, line, sizeof(line));
    }
    gpio_put(FUSE_VAULT_TFT_CHIP_SELECT_PIN, true);
    return true;
#endif
}

const fv_display_ops_t fv_rp2354_display_ops = {
    .initialize = initialize,
    .present = present,
};
