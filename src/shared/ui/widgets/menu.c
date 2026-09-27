#include "menu.h"
#include "drawing.h"

void ui_selection_move(unsigned *cursor, unsigned count, fv_ui_key key) {
    if (!count)
        return;
    if (*cursor >= count)
        *cursor = 0;
    if (key == UI_UP)
        *cursor = (*cursor + count - 1) % count;
    if (key == UI_DOWN)
        *cursor = (*cursor + 1) % count;
}

void ui_menu_key(fv_ui *u, const ui_menu *menu, fv_ui_key key) {
    ui_selection_move(&u->cursor, menu->count, key);
    if (key == UI_SELECT && u->cursor < menu->count) {
        void (*activate)(fv_ui *) = menu->items[u->cursor].activate;
        if (activate)
            activate(u);
    }
}

void ui_menu_render(fv_ui *u, const ui_menu *menu) {
    unsigned first = u->cursor >= menu->visible_rows ? u->cursor - menu->visible_rows + 1 : 0;
    for (unsigned i = first; i < menu->count && i < first + menu->visible_rows; ++i) {
        unsigned y = menu->y + (i - first) * menu->spacing;
        bool selected = u->cursor == i;
        if (selected) {
            for (unsigned row = y; row < y + menu->row_height; ++row)
                for (unsigned x = 3; x < 157; ++x)
                    ui_draw_pixel(u, x, row, true);
        }
        ui_draw_text_at(u, 7, y + menu->text_offset, menu->items[i].label, 1, !selected);
    }
}

void ui_confirmation_key(fv_ui *u, fv_ui_key key, void (*confirm)(fv_ui *),
                         void (*cancel)(fv_ui *)) {
    ui_selection_move(&u->cursor, 2, key);
    if (key == UI_BACK || (key == UI_SELECT && !u->cursor))
        cancel(u);
    else if (key == UI_SELECT)
        confirm(u);
}
