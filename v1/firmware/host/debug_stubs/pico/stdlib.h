#include "../../sd_stubs/pico/stdlib.h"
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t time);
