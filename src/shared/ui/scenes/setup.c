#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include <stdio.h>

static void stack_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK) {
        ui_go_home(u);
        return;
    }
    ui_selection_move(&u->cursor, u->job.count + 3, key);
    if ((key == UI_LEFT || key == UI_RIGHT || key == UI_SELECT) && u->cursor < u->job.count) {
        u->job.algorithms[u->cursor] = u->job.algorithms[u->cursor] % 3 + 1;
    } else if (key == UI_SELECT) {
        if (u->cursor == u->job.count) {
            if (u->job.count < 4)
                u->job.algorithms[u->job.count++] = 1;
        } else if (u->cursor == u->job.count + 1u) {
            if (u->job.count > 1) {
                u->job.algorithms[--u->job.count] = 0;
                u->cursor = u->job.count + 1;
            }
        } else {
            ui_scene_show(u, UI_OPTIONS);
        }
    }
}

static void options_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK) {
        ui_go_home(u);
        return;
    }
    ui_selection_move(&u->cursor, 3, key);
    if (key == UI_LEFT || key == UI_RIGHT) {
        if (u->cursor == 0) {
            if (key == UI_RIGHT && u->job.attempts < 100)
                ++u->job.attempts;
            if (key == UI_LEFT && u->job.attempts > 1)
                --u->job.attempts;
        } else if (u->cursor == 1) {
            u->job.action = u->job.action == 1 ? 2 : 1;
        }
    }
    if (key == UI_SELECT) {
        if (u->cursor < 2)
            ++u->cursor;
        else
            ui_scene_show(u, UI_REVIEW);
    }
}

static void save_review(fv_ui *u) {
    fv_ui_operation operation = UI_CREATE;
    if (u->flow == UI_FLOW_CHANGE)
        operation = UI_CHANGE;
    else if (u->flow == UI_FLOW_POLICY)
        operation = UI_POLICY;
    ui_submit(u, operation);
}

static void review_key(fv_ui *u, fv_ui_key key) {
    ui_confirmation_key(u, key, save_review, ui_go_home);
}

static void render_stack(fv_ui *u) {
    char b[40];
    ui_draw_header(u, "ENCRYPTION ORDER", NULL);
    unsigned first = u->cursor >= 4 ? u->cursor - 3 : 0;
    for (unsigned i = first; i < u->job.count + 3u && i < first + 4; i++) {
        if (i < u->job.count)
            snprintf(b, sizeof(b), "%u %s", i + 1,
                     u->job.algorithms[i] == 1   ? "AES-256"
                     : u->job.algorithms[i] == 2 ? "CAMELLIA-256"
                                                 : "SM4-128");
        else
            snprintf(b, sizeof(b), "%s",
                     i == u->job.count        ? "ADD LAYER (MAX 4)"
                     : i == u->job.count + 1u ? "REMOVE LAST LAYER"
                                              : "CONTINUE");
        ui_draw_choice(u, 16 + (i - first) * 12, b, u->cursor == i);
    }
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

static void render_options(fv_ui *u) {
    char b[40];
    ui_draw_header(u, "FAILURE POLICY", NULL);
    snprintf(b, sizeof(b), "ATTEMPTS: %u", (unsigned)u->job.attempts);
    ui_draw_choice(u, 16, b, u->cursor == 0);
    ui_draw_choice(u, 28, u->job.action == 1 ? "THEN: DESTROY KEY" : "THEN: LOCKOUT",
                   u->cursor == 1);
    ui_draw_choice(u, 40, "REVIEW", u->cursor == 2);
    if (u->cursor < 2)
        ui_draw_text_at(u, 4, 55, "\003\001 CHANGE", 1, UI_COLOUR_TEXT);
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

static void render_review(fv_ui *u) {
    char b[40];
    ui_draw_header(u,
                   (u->flow == UI_FLOW_CHANGE)   ? "CHANGE UNLOCK?"
                   : (u->flow == UI_FLOW_POLICY) ? "SAVE FAILURE POLICY?"
                                                 : "CREATE VAULT?",
                   NULL);
    ui_draw_text_at(u, 4, 16,
                    (u->flow == UI_FLOW_CHANGE)   ? "KEEPS FILES AND PASSKEYS"
                    : (u->flow == UI_FLOW_POLICY) ? "CHECKS CURRENT CREDENTIAL"
                                                  : "ERASES EXISTING SD DATA",
                    1, UI_COLOUR_TEXT);
    snprintf(b, sizeof(b), "%u FAILURES: %s", (unsigned)u->job.attempts,
             u->job.action == 1 ? "DESTROY" : "LOCKOUT");
    ui_draw_text_at(u, 4, 26, b, 1, UI_COLOUR_TEXT);
    if (u->flow != UI_FLOW_POLICY && u->flow != UI_FLOW_CHANGE) {
        snprintf(b, sizeof(b), "%u ENCRYPTION LAYERS", u->job.count);
        ui_draw_text_at(u, 4, 36, b, 1, UI_COLOUR_TEXT);
    }
    if (u->flow == UI_FLOW_SETUP) ui_draw_danger_choices(u, "CREATE VAULT");
    else ui_draw_confirm_choices(u, "SAVE");
}

const ui_scene ui_stack_scene = {.key = stack_key, .render = render_stack};
const ui_scene ui_options_scene = {.key = options_key, .render = render_options};
const ui_scene ui_review_scene = {.key = review_key, .render = render_review};
