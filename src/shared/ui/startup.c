#include "startup.h"
#include "startup_frames.inc"
uint8_t fv_startup_pixel(unsigned frame, unsigned pixel) {
    if(frame >= FV_STARTUP_FRAMES || pixel >= 12800) return 0;
    uint8_t packed = startup_frames[frame][pixel / 2];
    return startup_palette[(pixel & 1) ? packed & 15 : packed >> 4];
}
