#ifndef FV_TEST_PICO_STDLIB_H
#define FV_TEST_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#define pico_board_cmake_set(...)
#define pico_board_cmake_set_default(...)
#include "fuse_vault.h"
#define GPIO_OUT true
#define GPIO_IN false
typedef uint64_t absolute_time_t;
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool output);
void gpio_put(unsigned pin, bool value);
bool gpio_get(unsigned pin);
void gpio_pull_up(unsigned pin);
void gpio_disable_pulls(unsigned pin);
absolute_time_t make_timeout_time_us(uint64_t delay);
bool time_reached(absolute_time_t deadline);
void sleep_ms(uint32_t delay);
#endif
