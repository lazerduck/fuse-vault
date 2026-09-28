#ifndef FV_MUX_TEST_STDLIB_H
#define FV_MUX_TEST_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#define pico_board_cmake_set(a, b)
#define pico_board_cmake_set_default(a, b)
#include "fuse_vault.h"
uint64_t time_us_64(void);
bool gpio_get(unsigned pin);
void gpio_put(unsigned pin, bool value);
#endif
