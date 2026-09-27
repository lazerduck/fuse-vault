#ifndef FV_PICO_BUTTONS_H
#define FV_PICO_BUTTONS_H
#include "device_ui.h"

void fv_pico_buttons_init(void);
/* Returns a physical direction/Select/Back, or zero when there is no event.
 * A changed generation requires a stable release before the next press. */
fv_ui_key fv_pico_buttons_poll(uint32_t generation);
#endif
