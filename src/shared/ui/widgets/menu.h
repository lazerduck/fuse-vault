#ifndef FV_UI_MENU_H
#define FV_UI_MENU_H
#include "device_ui.h"

typedef struct {
    const char *label;
    void (*activate)(fv_ui *);
} ui_menu_item;

typedef struct {
    const ui_menu_item *items;
    unsigned count;
    unsigned y, spacing, row_height, text_offset, visible_rows;
} ui_menu;

/* Same definition drives both selection and rendering. Menu state belongs to
 * the active UI instance; static menu descriptions contain no mutable state. */
void ui_menu_key(fv_ui *, const ui_menu *, fv_ui_key);
void ui_menu_render(fv_ui *, const ui_menu *);
void ui_selection_move(unsigned *cursor, unsigned count, fv_ui_key);
/* Cancel is index zero. Back and selecting Cancel both invoke cancel. */
void ui_confirmation_key(fv_ui *, fv_ui_key, void (*confirm)(fv_ui *), void (*cancel)(fv_ui *));
#endif
