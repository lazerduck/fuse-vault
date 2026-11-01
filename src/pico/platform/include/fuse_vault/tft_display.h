#ifndef FV_TFT_DISPLAY_H
#define FV_TFT_DISPLAY_H
#include <stdint.h>
#include <stdbool.h>
#define FV_TFT_IDLE_US UINT64_C(60000000)
/* Core 0 only. Init runs before USB starts; poll writes at most one row. */
void fv_tft_init(void);
void fv_tft_poll(const uint8_t *framebuffer);
/* Display policy only; never changes vault authorization or its idle timer.
 * Zero keeps the display awake while unlocked, then sleeps after 60 seconds
 * locked. UINT16_MAX never sleeps; timed choices are 30/60/120/300/600 seconds.
 * Invalid values use zero. A changed policy or lock state wakes/restarts the
 * display timer; repeated identical calls do not count as activity.
 * Init restores the zero policy and locked state. Core 0 only. */
void fv_tft_set_sleep(uint16_t seconds, bool unlocked);
/* Core 0 activity: restart idle timer and wake. Returns true when asleep or
 * still dark during a refresh, so physical keys are consumed until the current
 * screen is visible. Repeated activity does not restart an ongoing refresh. */
bool fv_tft_activity(void);
/* Call once immediately after init, before USB starts. Flushes the startup
 * frame, then animates for 500 ms without blocking subsequent polls. */
void fv_tft_show_startup(void);
#endif
