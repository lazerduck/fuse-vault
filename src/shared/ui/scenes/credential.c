#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include <stdio.h>
#include <string.h>

static void choose_pattern(fv_ui *u) {
    u->job.profile = 2;
    ui_scene_show(u, UI_SECRET);
}
static void choose_wheels(fv_ui *u) {
    u->job.profile = 3;
    ui_scene_show(u, UI_SECRET);
}
static void choose_words(fv_ui *u) {
    u->job.profile = 4;
    ui_scene_show(u, UI_SECRET);
}
static const ui_menu_item method_items[] = {
    {"DIRECTION PATTERN", choose_pattern},
    {"FOUR CODE WHEELS", choose_wheels},
    {"FOUR WORDS", choose_words},
};
static const ui_menu method_menu = {method_items, 3, 18, 13, 9, 1, 3};

static void method_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK)
        ui_go_home(u);
    else
        ui_menu_key(u, &method_menu, key);
}

static void render_method(fv_ui *u) {
    ui_draw_header(u, "UNLOCK METHOD", NULL);
    ui_menu_render(u, &method_menu);
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

static void credential_enter(fv_ui *u) {
    fv_entry_begin(&u->entry, u->job.profile);
}

static void credential_submitted(fv_ui *u, const uint8_t *input, uint8_t length) {
    if (u->fido_modal) {
        u->fido_done = true;
        u->fido_approved = true;
    } else if (u->flow == UI_FLOW_CHANGE && !u->job.current_length) {
        memcpy(u->job.current, u->job.secret, u->job.length);
        u->job.current_length = u->job.length;
        fv_ui_wipe(u->job.secret, sizeof(u->job.secret));
        u->job.length = 0;
        fv_ui_wipe(&u->entry, sizeof(u->entry));
        ui_scene_show(u, UI_METHOD);
    } else if (u->flow == UI_FLOW_POLICY) {
        fv_ui_wipe(&u->entry, sizeof(u->entry));
        ui_scene_show(u, UI_OPTIONS);
    } else if (u->flow == UI_FLOW_UNLOCK) {
        ui_submit(u, UI_UNLOCK);
    } else if (u->screen == UI_SECRET) {
        if (u->job.profile != 2 || length >= 8)
            ui_scene_show(u, UI_CONFIRM);
        else
            u->error = 2;
    } else if (u->job.length == length && !memcmp(u->job.secret, input, length)) {
        fv_ui_wipe(u->confirmation, sizeof(u->confirmation));
        u->confirmation_length = 0;
        fv_ui_wipe(&u->entry, sizeof(u->entry));
        ui_scene_show(u, u->flow == UI_FLOW_CHANGE ? UI_REVIEW : UI_STACK);
    } else {
        fv_ui_wipe(u->job.secret, sizeof(u->job.secret));
        u->job.length = 0;
        fv_ui_wipe(u->confirmation, sizeof(u->confirmation));
        u->confirmation_length = 0;
        u->error = 1;
        ui_scene_show(u, UI_SECRET);
    }
}

static void credential_key(fv_ui *u, fv_ui_key key) {
    uint8_t *input = u->screen == UI_SECRET ? u->job.secret : u->confirmation;
    uint8_t *length = u->screen == UI_SECRET ? &u->job.length : &u->confirmation_length;
    bool submitted = false;
    if (u->job.profile == 3 || u->job.profile == 4) {
        if (key <= UI_RIGHT)
            u->error = 0;
        int result = fv_entry_key(&u->entry, key);
        if (result < 0) {
            ui_go_home(u);
            return;
        }
        if (result > 0) {
            u->error = 0;
            memcpy(input, u->entry.values, 4);
            *length = 4;
            submitted = true;
        }
    } else if (key <= UI_RIGHT) {
        u->error = 0;
        if (*length < FV_UI_SECRET_MAX)
            input[(*length)++] = (uint8_t)key;
    } else if (key == UI_BACK) {
        if (*length)
            input[--*length] = 0;
        else
            ui_go_home(u);
    } else if (*length) {
        submitted = true;
    }
    if (submitted)
        credential_submitted(u, input, *length);
}

static void credential_tile(fv_ui *u, unsigned x, unsigned y, unsigned width,
                            unsigned height, fv_ui_colour colour) {
    for (unsigned dy = 0; dy < height; ++dy)
        for (unsigned dx = 0; dx < width; ++dx)
            ui_draw_pixel(u, x + dx, y + dy, colour);
}

static void credential_controls(fv_ui *u) {
    const fv_credential_entry *e = &u->entry;
    char label[27];
    if (u->error == 1)
        snprintf(label, sizeof(label), "MISMATCH - TRY AGAIN");
    else if (e->profile == 3)
        snprintf(label, sizeof(label), "CODE %u/4", e->selected + 1);
    else if (e->count == 4)
        snprintf(label, sizeof(label), "REVIEW / ARROWS TO EDIT");
    else
        snprintf(label, sizeof(label), "WORD %u/4  STEP %u/3", e->count + 1, e->depth + 1);
    ui_draw_text_at(u, 4, 16, label, 1,
                    u->error == 1 ? UI_COLOUR_WARNING : UI_COLOUR_MUTED);
    if (e->profile == 3) {
        for (unsigned i = 0; i < 4; ++i) {
            unsigned x = 4 + i * 39;
            bool selected = i == e->selected;
            credential_tile(u, x, 26, 35, 38, 0x05);
            snprintf(label, sizeof(label), "%02u", (e->values[i] + 1) % 100);
            ui_draw_text_at(u, x + 12, 27, label, 1, UI_COLOUR_MUTED);
            if (selected)
                credential_tile(u, x, 36, 35, 18, UI_COLOUR_ACCENT);
            snprintf(label, sizeof(label), "%02u", e->values[i]);
            ui_draw_text_at(u, x + 6, 38, label, 2,
                            selected ? UI_COLOUR_BACKGROUND : UI_COLOUR_TEXT);
            snprintf(label, sizeof(label), "%02u", (e->values[i] + 99) % 100);
            ui_draw_text_at(u, x + 12, 56, label, 1, UI_COLOUR_MUTED);
        }
    } else {
        /* Explicit arrows preserve the directional mapping in both entry and review. */
        const unsigned xs[] = {4, 83, 83, 4}, ys[] = {27, 27, 46, 46};
        const char arrows[] = {4, 1, 2, 3};
        for (unsigned i = 0; i < 4; ++i) {
            unsigned x = xs[i], y = ys[i];
            credential_tile(u, x, y, 73, 17, 0x05);
            char arrow[] = {arrows[i], 0};
            ui_draw_text_at(u, x + 4, y + 5, arrow, 1, UI_COLOUR_ACCENT);
            if (e->count == 4)
                snprintf(label, sizeof(label), "%u %s", i + 1, fv_entry_word(e->values[i]));
            else {
                unsigned size = 16u >> (2 * e->depth), start = (e->prefix * 4u + i) * size;
                if (size == 1)
                    snprintf(label, sizeof(label), "%s", fv_entry_word(start));
                else
                    snprintf(label, sizeof(label), "%.3s-%.3s", fv_entry_word(start),
                             fv_entry_word(start + size - 1));
            }
            ui_draw_text_at(u, x + 17, y + 5, label, 1, UI_COLOUR_TEXT);
        }
    }
    bool cancel = u->fido_modal || e->profile == 3 || (!e->count && !e->depth);
    ui_draw_footer(u, cancel ? "\005 CANCEL" : "\005 BACK",
                   e->profile == 3 || e->count == 4 ? "\006 DONE" : NULL);
}

static const char *credential_title(const fv_ui *u) {
    if (u->fido_modal)
        return "FIDO: VERIFY UNLOCK";
    if (u->screen == UI_CONFIRM)
        return "CONFIRM CREDENTIAL";
    if (u->flow == UI_FLOW_POLICY || (u->flow == UI_FLOW_CHANGE && !u->job.current_length))
        return "CURRENT CREDENTIAL";
    if (u->flow == UI_FLOW_UNLOCK)
        return "UNLOCK VAULT";
    return "NEW CREDENTIAL";
}

static void direction_controls(fv_ui *u) {
    unsigned n = u->screen == UI_CONFIRM ? u->confirmation_length : u->job.length;
    const uint8_t *input = u->screen == UI_CONFIRM ? u->confirmation : u->job.secret;
    const char arrows[] = {0, 4, 2, 3, 1};
    unsigned first = n > 16 ? n - 16 : 0;
    char label[27];
    ui_draw_header(u, credential_title(u), NULL);
    if (u->error == 1)
        snprintf(label, sizeof(label), "MISMATCH - TRY AGAIN");
    else if (u->error == 2)
        snprintf(label, sizeof(label), "USE AT LEAST 8 INPUTS");
    else if (!n)
        snprintf(label, sizeof(label), "ENTER DIRECTIONS");
    else if (first)
        snprintf(label, sizeof(label), "%u-%u OF %u INPUTS", first + 1, n, n);
    else
        snprintf(label, sizeof(label), "%u INPUT%s", n, n == 1 ? "" : "S");
    ui_draw_text_at(u, 4, 16, label, 1,
                    u->error ? UI_COLOUR_WARNING : UI_COLOUR_MUTED);
    for (unsigned slot = 0; slot < 16; ++slot) {
        unsigned index = first + slot;
        unsigned x = 4 + (slot % 8) * 19, y = 27 + (slot / 8) * 19;
        bool filled = index < n, latest = filled && index + 1 == n;
        fv_ui_colour fill = latest ? UI_COLOUR_ACCENT : 0x05;
        for (unsigned dy = 0; dy < 17; ++dy)
            for (unsigned dx = 0; dx < 17; ++dx) {
                bool edge = !dx || dx == 16 || !dy || dy == 16;
                ui_draw_pixel(u, x + dx, y + dy,
                              !filled && index == n && edge ? UI_COLOUR_MUTED : fill);
            }
        if (filled) {
            char glyph[] = {input[index] >= 1 && input[index] <= 4 ? arrows[input[index]] : '?', 0};
            ui_draw_text_at(u, x + 4, y + 2, glyph, 2,
                            latest ? UI_COLOUR_BACKGROUND : UI_COLOUR_TEXT);
        } else {
            ui_draw_pixel(u, x + 8, y + 8, UI_COLOUR_MUTED);
        }
    }
    ui_draw_footer(u, u->fido_modal || !n ? "\005 CANCEL" : "\005 DELETE",
                   n ? "\006 DONE" : NULL);
}

static void render_secret(fv_ui *u) {
    if (u->job.profile == 3 || u->job.profile == 4) {
        ui_draw_header(u, credential_title(u), NULL);
        credential_controls(u);
    } else {
        direction_controls(u);
    }
}

const ui_scene ui_method_scene = {.key = method_key, .render = render_method};
const ui_scene ui_credential_scene = {
    .enter = credential_enter, .key = credential_key, .render = render_secret};
