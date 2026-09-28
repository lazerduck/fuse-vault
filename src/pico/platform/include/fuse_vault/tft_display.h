#ifndef FV_TFT_DISPLAY_H
#define FV_TFT_DISPLAY_H
#include <stdint.h>
/* Core 0 only. Init runs before USB starts; poll writes at most one row. */
void fv_tft_init(void);
void fv_tft_poll(const uint8_t *framebuffer);
/* Call once immediately after init, before USB starts. Flushes the startup
 * frame, then animates for 500 ms without blocking subsequent polls. */
void fv_tft_show_startup(void);
#endif
