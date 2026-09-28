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

static void credential_controls(fv_ui *u) {
    const fv_credential_entry *e = &u->entry;
    char label[27];
    ui_draw_rule(u, 10);
    ui_draw_rule(u, 67);
    ui_draw_text_at(u, 4, 71, u->fido_modal ? "\005 CANCEL" : "\005 BACK", 1, UI_COLOUR_TEXT);
    if (e->profile == 3 || e->count == 4)
        ui_draw_text_at(u, 118, 71, "\006 DONE", 1, UI_COLOUR_TEXT);
    if (e->profile == 3) {
        for (unsigned i = 0; i < 4; i++) {
            unsigned x = 5 + i * 39;
            snprintf(label, sizeof(label), "%02u", (e->values[i] + 1) % 100);
            ui_draw_text_at(u, x + 11, 18, label, 1, UI_COLOUR_TEXT);
            if (i == e->selected)
                for (unsigned y = 30; y < 50; y++)
                    for (unsigned dx = 0; dx < 34; dx++)
                        ui_draw_pixel(u, x + dx, y, UI_COLOUR_ACCENT);
            snprintf(label, sizeof(label), "%02u", e->values[i]);
            ui_draw_text_at(u, x + 6, 33, label, 2, i == e->selected ? UI_COLOUR_BACKGROUND : UI_COLOUR_TEXT);
            snprintf(label, sizeof(label), "%02u", (e->values[i] + 99) % 100);
            ui_draw_text_at(u, x + 11, 55, label, 1, UI_COLOUR_TEXT);
        }
    } else {
        if (e->count == 4)
            snprintf(label, sizeof(label), "REVIEW WORDS");
        else
            snprintf(label, sizeof(label), "WORD %u/4", e->count + 1);
        ui_draw_text_at(u, (160 - (unsigned)strlen(label) * 6) / 2, 14, label, 1, UI_COLOUR_TEXT);
        for (unsigned i = 0; i < 4; i++) {
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
            unsigned width = (unsigned)strlen(label) * 6 - 1;
            unsigned x = i == 3 ? 4 : i == 1 ? 156 - width : (160 - width) / 2;
            unsigned y = i == 0 ? 27 : i == 2 ? 55 : 41;
            ui_draw_text_at(u, x, y, label, 1, UI_COLOUR_TEXT);
        }
        ui_draw_text_at(u, 77, 41, "+", 1, UI_COLOUR_TEXT);
    }
    if (u->error == 1) {
        for (unsigned y = 11; y < 25; y++)
            for (unsigned x = 0; x < 160; x++)
                ui_draw_pixel(u, x, y, UI_COLOUR_BACKGROUND);
        ui_draw_text_at(u, 14, 15, "MISMATCH - TRY AGAIN", 1, UI_COLOUR_WARNING);
    }
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

static void render_secret(fv_ui *u) {
    char b[40];
    ui_draw_line(u, 0, credential_title(u));
    if (u->job.profile == 3 || u->job.profile == 4) {
        credential_controls(u);
    } else {
        if (u->error == 1)
            ui_draw_line(u, 1, "MISMATCH - START AGAIN");
        if (u->error == 2)
            ui_draw_line(u, 1, "USE AT LEAST 8 INPUTS");
        unsigned n = u->screen == UI_CONFIRM ? u->confirmation_length : u->job.length;
        const uint8_t *p = u->screen == UI_CONFIRM ? u->confirmation : u->job.secret;
        const char arrows[] = {0, 4, 2, 3, 1};
        snprintf(b, sizeof(b), "%u INPUTS", n);
        ui_draw_line(u, 2, b);
        for (unsigned row = 0; row < 3; row++) {
            unsigned j = 0;
            while (row * 22 + j < n && j < 22) {
                b[j] = p[row * 22 + j] <= 4 ? arrows[p[row * 22 + j]] : '?';
                j++;
            }
            b[j] = 0;
            ui_draw_line(u, row + 3, b);
        }
        ui_draw_line(u, 6,
                     u->fido_modal ? "SELECT: DONE BACK: CANCEL" : "SELECT: DONE BACK: DELETE");
        ui_draw_line(u, 7,
                     u->fido_modal ? "DIRECTIONS: ENTER PATTERN"
                     : n           ? "BACK AT EMPTY: CANCEL"
                                   : "BACK: CANCEL");
    }
}

const ui_scene ui_method_scene = {.key = method_key, .render = render_method};
const ui_scene ui_credential_scene = {
    .enter = credential_enter, .key = credential_key, .render = render_secret};
