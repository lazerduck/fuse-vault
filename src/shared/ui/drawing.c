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
                    ui_draw_pixel(u, x + b, row * 10 + y, UI_COLOUR_TEXT);
                }
}
/* Pixel-positioned credential controls share the same framebuffer/flip path. */
void ui_draw_pixel(fv_ui *u, unsigned x, unsigned y, fv_ui_colour colour) {
    if (x >= 160 || y >= 80)
        return;
    u->framebuffer[y * FV_SCREEN_WIDTH + x] = colour;
}
void ui_draw_text_at(fv_ui *u, unsigned x, unsigned y, const char *s, unsigned scale, fv_ui_colour ink) {
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
        ui_draw_pixel(u, x, y, UI_COLOUR_MUTED);
}
void ui_draw_header(fv_ui *u, const char *title, const char *detail) {
    ui_draw_text_at(u, 4, 2, title, 1, UI_COLOUR_ACCENT);
    ui_draw_rule(u, 12);
    if (detail)
        ui_draw_text_at(u, 156 - (unsigned)strlen(detail) * 6, 2, detail, 1, UI_COLOUR_TEXT);
}
void ui_draw_footer(fv_ui *u, const char *back, const char *action) {
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, back, 1, UI_COLOUR_TEXT);
    if (action)
        ui_draw_text_at(u, 156 - (unsigned)strlen(action) * 6, 71, action, 1, UI_COLOUR_TEXT);
}
void ui_draw_choice(fv_ui *u, unsigned y, const char *label, bool selected) {
    if (selected)
        for (unsigned r = y; r < y + 9; r++)
            for (unsigned x = 3; x < 157; x++)
                ui_draw_pixel(u, x, r, UI_COLOUR_ACCENT);
    ui_draw_text_at(u, 7, y + 1, label, 1, selected ? UI_COLOUR_BACKGROUND : UI_COLOUR_TEXT);
}
void ui_draw_confirm_choices(fv_ui *u, const char *action) {
    ui_draw_choice(u, 47, "CANCEL", !u->cursor);
    ui_draw_choice(u, 57, action, u->cursor != 0);
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

/* Destructive confirmations retain Cancel as the initial selection. */
void ui_draw_danger_choices(fv_ui *u, const char *action) {
    ui_draw_choice(u, 47, "CANCEL", !u->cursor);
    if (u->cursor)
        for (unsigned y = 57; y < 66; ++y)
            for (unsigned x = 3; x < 157; ++x) ui_draw_pixel(u, x, y, UI_COLOUR_ERROR);
    ui_draw_text_at(u, 7, 58, action, 1, u->cursor ? UI_COLOUR_TEXT : UI_COLOUR_WARNING);
    ui_draw_footer(u, "\005 BACK", u->cursor ? "\006 CONFIRM" : "\006 CANCEL");
}

/* Ten 50 ms frames: connections converge, fuse, then lock into a vault. */
void ui_draw_splash(fv_ui *u, unsigned frame) {
    if (frame > 9) frame = 9;
    memset(u->framebuffer, 0, sizeof(u->framebuffer));
    const fv_ui_colour blue = 0x17, amber = 0xf0;
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned branch = 0; branch < 3; ++branch) {
            for (unsigned step = 0; step <= 43; ++step) {
                unsigned x = side ? 123 - step : 36 + step;
                int offset = ((int)branch - 1) * 13;
                unsigned y = (unsigned)(23 + (step < 18 ? offset : offset * (43-(int)step) / 25));
                bool pulse = frame < 6 && step / 8 == frame;
                ui_draw_pixel(u, x, y, pulse ? amber : blue);
                ui_draw_pixel(u, x, y+1, pulse ? amber : blue);
            }
            unsigned cx = side ? 125 : 34, cy = 23 + ((int)branch - 1)*13;
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx) {
                    int d = dx*dx+dy*dy;
                    if (d >= 5 && d <= 12) ui_draw_pixel(u,cx+dx,cy+dy,blue);
                }
        }
    /* Fusion glow contracts into the illuminated centre of the vault door. */
    unsigned radius = frame < 5 ? 2 : frame < 8 ? 9-(frame-5)*2 : 3;
    for (int dy = -(int)radius; dy <= (int)radius; ++dy)
        for (int dx = -(int)radius; dx <= (int)radius; ++dx)
            if ((unsigned)(dx*dx+dy*dy) <= radius*radius)
                ui_draw_pixel(u,79+dx,24+dy,dx*dx+dy*dy < 5 ? 0xff : amber);
    if (frame >= 7) {
        for (unsigned y = 8; y <= 39; ++y) {
            unsigned inset = y < 12 ? 12-y : y > 35 ? y-35 : 0;
            for (unsigned x = 63+inset; x <= 96-inset; ++x)
                if (x == 63+inset || x == 96-inset || y == 8 || y == 39)
                    ui_draw_pixel(u,x,y,blue);
        }
        /* Four locking bolts and a central combination dial. */
        for (unsigned n=0;n<4;++n) {
            ui_draw_pixel(u,79,14+n,UI_COLOUR_TEXT);
            ui_draw_pixel(u,79,31+n,UI_COLOUR_TEXT);
            ui_draw_pixel(u,69+n,24,UI_COLOUR_TEXT);
            ui_draw_pixel(u,86+n,24,UI_COLOUR_TEXT);
        }
    }
    ui_draw_text_at(u,21,47,"FUSE VAULT",2,UI_COLOUR_TEXT);
    ui_draw_text_at(u,29,68,"PRIVATE BY DESIGN",1,blue);
}

/* Small vault door: bevelled enclosure, four bolts, and a central dial. */
void ui_draw_vault(fv_ui *u, unsigned x, unsigned y, fv_ui_colour colour) {
    for (unsigned dy = 0; dy < 25; ++dy)
        for (unsigned dx = 0; dx < 25; ++dx) {
            unsigned inset = dy < 3 ? 3-dy : dy > 21 ? dy-21 : 0;
            if (dx < inset || dx > 24-inset) continue;
            fv_ui_colour ink = 0x05;
            if (dx == inset || dx == 24-inset || dy == 0 || dy == 24) ink = colour;
            int a = (int)dx-12, b = (int)dy-12;
            int d = a*a+b*b;
            if (d >= 13 && d <= 25) ink = colour;
            if ((!a && b >= -2 && b <= 2) || (!b && a >= -2 && a <= 2)) ink = UI_COLOUR_TEXT;
            if ((dx == 12 && (dy == 4 || dy == 20)) ||
                (dy == 12 && (dx == 4 || dx == 20))) ink = UI_COLOUR_TEXT;
            ui_draw_pixel(u, x+dx, y+dy, ink);
        }
}
