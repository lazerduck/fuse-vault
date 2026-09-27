#ifndef FV_UI_DRAWING_H
#define FV_UI_DRAWING_H
#include "device_ui.h"

/* Drawing touches only the framebuffer. Coordinates use the logical orientation;
 * the core rotates the completed frame once, after the active scene renders. */
void ui_draw_line(fv_ui *, unsigned row, const char *text);
void ui_draw_pixel(fv_ui *, unsigned x, unsigned y, bool on);
void ui_draw_text_at(fv_ui *, unsigned x, unsigned y, const char *text, unsigned scale, bool ink);
void ui_draw_rule(fv_ui *, unsigned y);
void ui_draw_header(fv_ui *, const char *title, const char *detail);
void ui_draw_footer(fv_ui *, const char *back, const char *action);
void ui_draw_choice(fv_ui *, unsigned y, const char *label, bool selected);
void ui_draw_confirm_choices(fv_ui *, const char *action);
#endif
