#ifndef FV_TFT_DISPLAY_H
#define FV_TFT_DISPLAY_H
#include <stdint.h>
#include <stdbool.h>
#define FV_TFT_IDLE_US UINT64_C(60000000)
/* Core 0 only. Init runs before USB starts; poll writes at most one row. */
void fv_tft_init(void);
void fv_tft_poll(const uint8_t *framebuffer);
/* Core 0 activity: restart idle timer and wake. Returns true when waking from
 * idle, so a physical wake key can be consumed instead of activating the UI. */
bool fv_tft_activity(void);
/* Call once immediately after init, before USB starts. Flushes the startup
 * frame, then animates for 500 ms without blocking subsequent polls. */
void fv_tft_show_startup(void);
#endif
