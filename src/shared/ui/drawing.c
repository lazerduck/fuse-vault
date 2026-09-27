#include "drawing.h"
#include <string.h>

/* Bitmap font reused from the archived V1 renderer. */
static uint8_t glyph_row(char character, unsigned row) {
    static const uint8_t letters[26][7] = {
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
        {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
        {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
        {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
        {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    };
    static const uint8_t digits[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},  {14, 17, 1, 2, 4, 8, 31},
        {30, 1, 1, 14, 1, 1, 30},     {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
        {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},   {14, 17, 17, 14, 17, 17, 14},
        {14, 17, 17, 15, 1, 1, 14},
    };

    if (row >= 7u)
        return 0u;
    if (character >= 'a' && character <= 'z')
        character -= (char)('a' - 'A');
    if (character >= 'A' && character <= 'Z')
        return letters[character - 'A'][row];
    if (character >= '0' && character <= '9')
        return digits[character - '0'][row];
    switch (character) {
    case '\001': {
        static const uint8_t g[7] = {4, 2, 1, 31, 1, 2, 4};
        return g[row];
    }
    case '\002': {
        static const uint8_t g[7] = {4, 4, 4, 21, 14, 4, 0};
        return g[row];
    }
    case '\003': {
        static const uint8_t g[7] = {4, 8, 16, 31, 16, 8, 4};
        return g[row];
    }
    case '\004': {
        static const uint8_t g[7] = {0, 4, 14, 21, 4, 4, 4};
        return g[row];
    }
    case '\005': {
        static const uint8_t g[7] = {8, 16, 31, 17, 1, 1, 14};
        return g[row];
    }
    case '\006': {
        static const uint8_t g[7] = {1, 1, 5, 9, 31, 8, 4};
        return g[row];
    }
    case '\007': {
        static const uint8_t g[7] = {14, 17, 17, 31, 27, 27, 31};
        return g[row];
    }
    case '>': {
        static const uint8_t g[7] = {16, 8, 4, 2, 4, 8, 16};
        return g[row];
    }
    case '<': {
        static const uint8_t g[7] = {1, 2, 4, 8, 4, 2, 1};
        return g[row];
    }
    case '[': {
        static const uint8_t g[7] = {14, 8, 8, 8, 8, 8, 14};
        return g[row];
    }
    case ']': {
        static const uint8_t g[7] = {14, 2, 2, 2, 2, 2, 14};
        return g[row];
    }
    case '(': {
        static const uint8_t g[7] = {2, 4, 8, 8, 8, 4, 2};
        return g[row];
    }
    case ')': {
        static const uint8_t g[7] = {8, 4, 2, 2, 2, 4, 8};
        return g[row];
    }
    case '@': {
        static const uint8_t g[7] = {14, 17, 23, 21, 23, 16, 14};
        return g[row];
    }
    case '?': {
        static const uint8_t g[7] = {14, 17, 1, 2, 4, 0, 4};
        return g[row];
    }
    case '_':
        return row == 6u ? 31u : 0u;
    case '+':
        return row == 3u ? 31u : (row >= 1u && row <= 5u ? 4u : 0u);
    case ':':
        return (row == 2u || row == 5u) ? 4u : 0u;
    case '/':
        return (uint8_t)(1u << (row < 5u ? row : 4u));
    case '-':
        return row == 3u ? 14u : 0u;
    case '.':
        return row == 6u ? 4u : 0u;
    case ' ':
        return 0u;
    default:
        return glyph_row('?', row);
    }
}

void ui_draw_line(fv_ui *u, unsigned row, const char *s) {
    if (row >= 8)
        return;
    for (unsigned x = 2; *s && x + 5 <= 160; x += 6, s++)
        for (unsigned y = 0; y < 7; y++)
            for (unsigned b = 0; b < 5; b++)
                if (glyph_row(*s, y) & (16u >> b)) {
                    unsigned bit = (row * 10 + y) * 160 + x + b;
                    u->framebuffer[bit / 8] |= 0x80u >> (bit % 8);
                }
}
/* Pixel-positioned credential controls share the same framebuffer/flip path. */
void ui_draw_pixel(fv_ui *u, unsigned x, unsigned y, bool on) {
    if (x >= 160 || y >= 80)
        return;
    unsigned bit = y * 160 + x;
    uint8_t mask = (uint8_t)(0x80u >> (bit % 8));
    if (on)
        u->framebuffer[bit / 8] |= mask;
    else
        u->framebuffer[bit / 8] &= (uint8_t)~mask;
}
void ui_draw_text_at(fv_ui *u, unsigned x, unsigned y, const char *s, unsigned scale, bool ink) {
    for (; *s; s++, x += 6 * scale)
        for (unsigned r = 0; r < 7; r++)
            for (unsigned c = 0; c < 5; c++)
                if (glyph_row(*s, r) & (16u >> c))
                    for (unsigned dy = 0; dy < scale; dy++)
                        for (unsigned dx = 0; dx < scale; dx++)
                            ui_draw_pixel(u, x + c * scale + dx, y + r * scale + dy, ink);
}
void ui_draw_rule(fv_ui *u, unsigned y) {
    for (unsigned x = 3; x < 157; x++)
        ui_draw_pixel(u, x, y, true);
}
void ui_draw_header(fv_ui *u, const char *title, const char *detail) {
    ui_draw_text_at(u, 4, 2, title, 1, true);
    ui_draw_rule(u, 12);
    if (detail)
        ui_draw_text_at(u, 156 - (unsigned)strlen(detail) * 6, 2, detail, 1, true);
}
void ui_draw_footer(fv_ui *u, const char *back, const char *action) {
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, back, 1, true);
    if (action)
        ui_draw_text_at(u, 156 - (unsigned)strlen(action) * 6, 71, action, 1, true);
}
void ui_draw_choice(fv_ui *u, unsigned y, const char *label, bool selected) {
    if (selected)
        for (unsigned r = y; r < y + 9; r++)
            for (unsigned x = 3; x < 157; x++)
                ui_draw_pixel(u, x, r, true);
    ui_draw_text_at(u, 7, y + 1, label, 1, !selected);
}
void ui_draw_confirm_choices(fv_ui *u, const char *action) {
    ui_draw_choice(u, 47, "CANCEL", !u->cursor);
    ui_draw_choice(u, 57, action, u->cursor != 0);
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}
