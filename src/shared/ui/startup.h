#ifndef FV_UI_STARTUP_H
#define FV_UI_STARTUP_H
#include <stdint.h>
#define FV_STARTUP_FRAMES 10u
#define FV_STARTUP_FRAME_US 50000u
/* Read a pixel of the exact flash artwork used by the physical display. */
uint8_t fv_startup_pixel(unsigned frame, unsigned pixel);
#endif
