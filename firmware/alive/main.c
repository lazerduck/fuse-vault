#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    uint32_t last = to_ms_since_boot(get_absolute_time());
    while (true) {
        int c = getchar_timeout_us(1000);
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (c == '?') printf("FV_ALIVE_V1 PONG %lu\n", (unsigned long)now);
        if ((uint32_t)(now - last) >= 1000) {
            printf("FV_ALIVE_V1 AWAKE %lu\n", (unsigned long)now);
            last = now;
        }
    }
}
