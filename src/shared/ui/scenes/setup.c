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
        u->job.algorithms[u->cursor] = u->job.algorithms[u->cursor] % 5 + 1;
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
    ui_selection_move(&u->cursor, 5, key);
    if (key == UI_LEFT || key == UI_RIGHT) {
        if (u->cursor == 0) {
            u->job.action = u->job.action == 5 ? 1 : 5;
            u->job.attempts = u->job.action == 5 ? 0 : 10;
        } else if (u->cursor == 1 && u->job.action == 1) {
            if (key == UI_RIGHT && u->job.attempts < 100)
                ++u->job.attempts;
            if (key == UI_LEFT && u->job.attempts > 1)
                --u->job.attempts;
        } else if (u->cursor == 2) {
            static const uint32_t durations[] = {0, 60, 120, 300, 600, 900, 1800};
            unsigned choice = 0;
            for (unsigned i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i)
                if (durations[i] == u->job.auto_lock_seconds) choice = i;
            choice = key == UI_RIGHT ? (choice + 1) % 7 : (choice + 6) % 7;
            u->job.auto_lock_seconds = durations[choice];
        } else if (u->cursor == 3) {
            static const uint16_t durations[] = {0, 30, 60, 120, 300, 600, UINT16_MAX};
            unsigned choice = 0;
            for (unsigned i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i)
                if (durations[i] == u->job.screen_sleep_seconds) choice = i;
            choice = key == UI_RIGHT ? (choice + 1) % 7 : (choice + 6) % 7;
            u->job.screen_sleep_seconds = durations[choice];
        }
    }
    if (key == UI_SELECT) {
        if (u->cursor < 4)
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
                     : u->job.algorithms[i] == 3 ? "SM4-128"
                     : u->job.algorithms[i] == 4 ? "ARIA-256"
                                                 : "TWOFISH-256");
        else
            snprintf(b, sizeof(b), "%s",
                     i == u->job.count        ? "ADD LAYER (MAX 4)"
                     : i == u->job.count + 1u ? "REMOVE LAST LAYER"
                                              : "CONTINUE");
        ui_draw_choice(u, 16 + (i - first) * 12, b, u->cursor == i);
    }
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

static void screen_sleep_label(char *label, size_t size, uint16_t seconds) {
    if (!seconds)
        snprintf(label, size, "SCREEN: UNTIL LOCK");
    else if (seconds == UINT16_MAX)
        snprintf(label, size, "SCREEN: NEVER SLEEP");
    else if (seconds < 60)
        snprintf(label, size, "SCREEN SLEEP: %u SEC", (unsigned)seconds);
    else
        snprintf(label, size, "SCREEN SLEEP: %u MIN", (unsigned)(seconds / 60));
}

static void render_options(fv_ui *u) {
    char b[40];
    snprintf(b, sizeof(b), "%u/5", u->cursor + 1);
    ui_draw_header(u, "SECURITY", b);
    unsigned first = u->cursor >= 4 ? u->cursor - 3 : 0;
    for (unsigned item = first; item < 5 && item < first + 4; ++item) {
        if (item == 0)
            snprintf(b, sizeof(b), "DESTROY KEYS: %s", u->job.action == 5 ? "OFF" : "ON");
        else if (item == 1) {
            if (u->job.action == 5) snprintf(b, sizeof(b), "UNLIMITED ATTEMPTS");
            else snprintf(b, sizeof(b), "AFTER: %u FAILURES", (unsigned)u->job.attempts);
        } else if (item == 2) {
            if (u->job.auto_lock_seconds)
                snprintf(b, sizeof(b), "AUTO LOCK: %u MIN", (unsigned)(u->job.auto_lock_seconds / 60));
            else snprintf(b, sizeof(b), "AUTO LOCK: OFF");
        } else if (item == 3)
            screen_sleep_label(b, sizeof(b), u->job.screen_sleep_seconds);
        else snprintf(b, sizeof(b), "REVIEW");
        ui_draw_choice(u, 16 + (item - first) * 11, b, u->cursor == item);
    }
    const char *hint = u->cursor == 2 ? "AFTER INACTIVITY"
                     : u->cursor == 1 && u->job.action == 1 ? "CONSECUTIVE FAILURES"
                     : u->cursor == 3 ? "DISPLAY ONLY"
                     : u->cursor == 4 ? "CHECK BEFORE SAVING" : "\003\001 CHANGE";
    ui_draw_text_at(u, 4, 60, hint, 1, UI_COLOUR_MUTED);
    ui_draw_footer(u, "\005 BACK", "\006 SELECT");
}

static void render_review(fv_ui *u) {
    char b[40], layers[16];
    snprintf(layers, sizeof(layers), "%u LAYERS", u->job.count);
    ui_draw_header(u,
                   (u->flow == UI_FLOW_CHANGE)   ? "CHANGE UNLOCK?"
                   : (u->flow == UI_FLOW_POLICY) ? "SAVE SECURITY?"
                                                 : "CREATE VAULT?",
                   u->flow == UI_FLOW_SETUP ? layers : NULL);
    ui_draw_text_at(u, 4, 14,
                    (u->flow == UI_FLOW_CHANGE)   ? "KEEPS FILES AND PASSKEYS"
                    : (u->flow == UI_FLOW_POLICY) ? "CHECKS CURRENT CREDENTIAL"
                                                  : "ERASES EXISTING SD DATA",
                    1, UI_COLOUR_TEXT);
    if (u->job.action == 5)
        snprintf(b, sizeof(b), "KEY DESTRUCTION DISABLED");
    else
        snprintf(b, sizeof(b), "%u FAILURES: %s", (unsigned)u->job.attempts,
                 u->job.action == 1 ? "DESTROY KEYS" : "LOCKOUT");
    ui_draw_text_at(u, 4, 22, b, 1, UI_COLOUR_TEXT);
    if (u->job.auto_lock_seconds)
        snprintf(b, sizeof(b), "AUTO LOCK: %u MIN IDLE", (unsigned)(u->job.auto_lock_seconds / 60));
    else
        snprintf(b, sizeof(b), "AUTO LOCK: OFF");
    ui_draw_text_at(u, 4, 30, b, 1, UI_COLOUR_TEXT);
    screen_sleep_label(b, sizeof(b), u->job.screen_sleep_seconds);
    ui_draw_text_at(u, 4, 38, b, 1, UI_COLOUR_TEXT);
    if (u->flow == UI_FLOW_SETUP) ui_draw_danger_choices(u, "CREATE VAULT");
    else ui_draw_confirm_choices(u, "SAVE");
}

const ui_scene ui_stack_scene = {.key = stack_key, .render = render_stack};
const ui_scene ui_options_scene = {.key = options_key, .render = render_options};
const ui_scene ui_review_scene = {.key = review_key, .render = render_review};
