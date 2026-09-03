#include "fuse_vault/ui.h"

#include <stddef.h>
#include <string.h>

#define RGB565(red, green, blue) \
    ((fv_pixel_t)((((red) & 0x1fu) << 11u) | \
                  (((green) & 0x3fu) << 5u) | ((blue) & 0x1fu)))

static const fv_pixel_t COLOR_BACKGROUND = RGB565(0u, 2u, 3u);
static const fv_pixel_t COLOR_TEXT = RGB565(27u, 58u, 29u);
static const fv_pixel_t COLOR_ACCENT = RGB565(3u, 47u, 22u);
static const fv_pixel_t COLOR_MUTED = RGB565(10u, 24u, 13u);

static uint8_t glyph_row(char character, unsigned row) {
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };

    if (row >= 7u) return 0u;
    if (character >= 'a' && character <= 'z') character -= (char)('a' - 'A');
    if (character >= 'A' && character <= 'Z') return letters[character - 'A'][row];
    if (character >= '0' && character <= '9') return digits[character - '0'][row];
    switch (character) {
        case '>': { static const uint8_t g[7] = {16,8,4,2,4,8,16}; return g[row]; }
        case '<': { static const uint8_t g[7] = {1,2,4,8,4,2,1}; return g[row]; }
        case '[': { static const uint8_t g[7] = {14,8,8,8,8,8,14}; return g[row]; }
        case ']': { static const uint8_t g[7] = {14,2,2,2,2,2,14}; return g[row]; }
        case ':': return (row == 2u || row == 5u) ? 4u : 0u;
        case '/': return (uint8_t)(1u << (row < 5u ? row : 4u));
        case '-': return row == 3u ? 14u : 0u;
        case '.': return row == 6u ? 4u : 0u;
        default: return 0u;
    }
}

static void set_pixel(fv_framebuffer_t *framebuffer, unsigned x, unsigned y,
                      fv_pixel_t color) {
    if (x < FV_DISPLAY_WIDTH && y < FV_DISPLAY_HEIGHT) {
        framebuffer->pixels[y][x] = color;
    }
}

static void horizontal_line(fv_framebuffer_t *framebuffer, unsigned y,
                            fv_pixel_t color) {
    if (y >= FV_DISPLAY_HEIGHT) return;
    for (unsigned x = 4u; x < FV_DISPLAY_WIDTH - 4u; ++x) {
        framebuffer->pixels[y][x] = color;
    }
}

static void text(fv_framebuffer_t *framebuffer, unsigned x, unsigned y,
                 const char *value, fv_pixel_t color) {
    while (*value != '\0' && x + 5u <= FV_DISPLAY_WIDTH) {
        for (unsigned row = 0u; row < 7u; ++row) {
            const uint8_t bits = glyph_row(*value, row);
            for (unsigned column = 0u; column < 5u; ++column) {
                if ((bits & (uint8_t)(1u << (4u - column))) != 0u) {
                    set_pixel(framebuffer, x + column, y + row, color);
                }
            }
        }
        x += 6u;
        ++value;
    }
}

void fv_ui_draw(const fv_app_t *app, fv_framebuffer_t *framebuffer) {
    if (app == NULL || framebuffer == NULL) return;

    fv_ui_view_t view;
    fv_app_render(app, &view);
    for (unsigned y = 0u; y < FV_DISPLAY_HEIGHT; ++y) {
        for (unsigned x = 0u; x < FV_DISPLAY_WIDTH; ++x) {
            framebuffer->pixels[y][x] = COLOR_BACKGROUND;
        }
    }

    text(framebuffer, 4u, 3u, view.title, COLOR_TEXT);
    horizontal_line(framebuffer, 13u, COLOR_ACCENT);
    for (size_t index = 0u; index < FV_UI_LINE_COUNT; ++index) {
        text(framebuffer, 4u, 19u + (unsigned)index * 11u,
             view.lines[index], COLOR_TEXT);
    }
    horizontal_line(framebuffer, 67u, COLOR_MUTED);
    text(framebuffer, 4u, 71u, "ARROWS ENTER BACK", COLOR_MUTED);
}
