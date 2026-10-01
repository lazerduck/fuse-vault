#include "scene.h"
#include "drawing.h"
#include "settings.h"
#include <stdio.h>

static void dashboard_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK) ui_submit(u, UI_LOCK);
    else if (key == UI_SELECT) ui_scene_show(u, UI_SETTINGS);
    else if (key == UI_LEFT || key == UI_RIGHT) u->flipped = !u->flipped;
}

static void speed_label(fv_ui *u, unsigned y, const char *name, uint32_t rate,
                        fv_ui_colour colour) {
    char label[27];
    if (rate >= 10240)
        snprintf(label, sizeof(label), "%s %lu.%lu MIB/S", name,
                 (unsigned long)(rate / 10240), (unsigned long)((rate % 10240) * 10 / 10240));
    else
        snprintf(label, sizeof(label), "%s %lu.%lu KIB/S", name,
                 (unsigned long)(rate / 10), (unsigned long)(rate % 10));
    for (unsigned dy = 0; dy < 7; ++dy)
        for (unsigned dx = 0; dx < 2; ++dx) ui_draw_pixel(u, 4+dx, y+dy, colour);
    ui_draw_text_at(u, 10, y, label, 1, colour);
}

static unsigned height(uint32_t rate, uint32_t peak) {
    return (unsigned)((uint64_t)rate * 12 / peak);
}

static void chart(fv_ui *u, const uint32_t *history, uint32_t rate, uint32_t peak,
                  unsigned bottom, fv_ui_colour colour) {
    fv_ui_colour shade = colour == UI_COLOUR_ACCENT ? 0x05 : 0x44;
    for (unsigned x = 4; x <= 155; ++x) {
        ui_draw_pixel(u, x, bottom, shade);
        if ((x-4)%15 == 0)
            for (unsigned y = bottom-12; y < bottom; y += 3)
                ui_draw_pixel(u, x, y, 0x05);
    }
    if (u->dashboard_bars) {
        unsigned filled = (unsigned)((uint64_t)rate * 152 / peak);
        for (unsigned y = bottom - 11; y <= bottom; ++y)
            for (unsigned x = 0; x < 152; ++x)
                ui_draw_pixel(u, x + 4, y, x%8 == 7 ? 0 : x < filled ? colour : 0x05);
        return;
    }
    /* Right-align recent samples; connect them without gaps. */
    unsigned start = (u->history_next + FV_UI_HISTORY_SAMPLES - u->history_count) % FV_UI_HISTORY_SAMPLES;
    unsigned previous_x = 0, previous_h = 0;
    for (unsigned i = 0; i < u->history_count; ++i) {
        unsigned x = 4 + (FV_UI_HISTORY_SAMPLES - u->history_count + i) * 151 / (FV_UI_HISTORY_SAMPLES - 1);
        unsigned h = height(history[(start + i) % FV_UI_HISTORY_SAMPLES], peak);
        if (i) {
            unsigned span = x - previous_x;
            for (unsigned dx = 0; dx <= span; ++dx) {
                int a = (int)previous_h + ((int)h - (int)previous_h) * (int)dx / (int)span;
                int b = dx == span ? (int)h : (int)previous_h + ((int)h - (int)previous_h) * (int)(dx + 1) / (int)span;
                int lo = a < b ? a : b, hi = a > b ? a : b;
                for (int y = 0; y < lo; ++y)
                    ui_draw_pixel(u, previous_x + dx, bottom - y, shade);
                for (int y = lo; y <= hi; ++y) ui_draw_pixel(u, previous_x + dx, bottom - y, colour);
            }
        } else ui_draw_pixel(u, x, bottom - h, colour);
        previous_x = x; previous_h = h;
    }
}

static void render_dashboard(fv_ui *u) {
    uint32_t peak = 10; /* Shared rolling scale, minimum 1 KiB/s. */
    for (unsigned i = 0; i < u->history_count; ++i) {
        if (u->read_history[i] > peak) peak = u->read_history[i];
        if (u->write_history[i] > peak) peak = u->write_history[i];
    }
    ui_draw_header(u, "VAULT OPEN", u->dashboard_bars ? "LIVE" : "30S");
    speed_label(u, 16, "UP", u->write_kib_tenths, UI_COLOUR_ACCENT);
    speed_label(u, 42, "DOWN", u->read_kib_tenths, UI_COLOUR_WARNING);
    chart(u, u->write_history, u->write_kib_tenths, peak, 39, UI_COLOUR_ACCENT);
    chart(u, u->read_history, u->read_kib_tenths, peak, 65, UI_COLOUR_WARNING);
    ui_draw_footer(u, "\005 LOCK", "\006 SETTINGS");
}

const ui_scene ui_dashboard_scene = {.key = dashboard_key, .render = render_dashboard};

static void dashboard_setting_enter(fv_ui *u) { u->cursor = u->dashboard_bars ? 1 : 0; }
static void dashboard_setting_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_UP || key == UI_DOWN) u->cursor = !u->cursor;
    else if (key == UI_SELECT || key == UI_BACK) {
        if (key == UI_SELECT) u->dashboard_bars = u->cursor != 0;
        ui_settings_return(u, u->fido_enabled ? UI_SETTING_DASHBOARD : UI_SETTING_FIDO_INIT);
    }
}
static void dashboard_setting_render(fv_ui *u) {
    ui_draw_header(u, "DEFAULT DASHBOARD", NULL);
    ui_draw_choice(u, 20, "TRANSFER HISTORY", u->cursor == 0);
    ui_draw_choice(u, 34, "LIVE SPEED BARS", u->cursor == 1);
    ui_draw_text_at(u, 4, 51, "USED AFTER UNLOCK", 1, UI_COLOUR_MUTED);
    ui_draw_footer(u, "\005 CANCEL", "\006 SAVE");
}
const ui_scene ui_dashboard_setting_scene = {
    .enter = dashboard_setting_enter, .key = dashboard_setting_key,
    .render = dashboard_setting_render
};
