#include <stdbool.h>
#include <stdint.h>
#define pico_board_cmake_set(a,b)
#define pico_board_cmake_set_default(a,b)
#include "fuse_vault.h"
#define GPIO_OUT 1
#define GPIO_FUNC_SPI 2
void gpio_init(unsigned pin);
void gpio_put(unsigned pin, bool value);
void gpio_set_dir(unsigned pin, bool out);
void gpio_set_function(unsigned pin, unsigned function);
void sleep_ms(unsigned ms);
uint64_t time_us_64(void);
