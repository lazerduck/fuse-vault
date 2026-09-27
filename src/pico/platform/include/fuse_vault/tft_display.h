#ifndef FV_TFT_DISPLAY_H
#define FV_TFT_DISPLAY_H
#include <stdint.h>
/* Core 0 only. Init runs before USB starts; poll writes at most one row. */
void fv_tft_init(void);
void fv_tft_poll(const uint8_t *framebuffer);
#endif
