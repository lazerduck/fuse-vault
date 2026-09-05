#include "fuse_vault/rp2354_input.h"

#include "fuse_vault/input.h"

#include "hardware/gpio.h"

#include <stddef.h>

static const uint8_t pins[FV_INPUT_COUNT] = {
    FUSE_VAULT_NAV_UP_PIN,
    FUSE_VAULT_NAV_DOWN_PIN,
    FUSE_VAULT_NAV_LEFT_PIN,
    FUSE_VAULT_NAV_RIGHT_PIN,
    FUSE_VAULT_NAV_SELECT_PIN,
    FUSE_VAULT_NAV_BACK_PIN,
};

bool fv_rp2354_input_init(void) {
    for (size_t index = 0u; index < FV_INPUT_COUNT; ++index) {
        gpio_init(pins[index]);
        gpio_set_dir(pins[index], GPIO_IN);
#if FUSE_VAULT_NAV_ACTIVE_LOW
        gpio_pull_up(pins[index]);
#else
        gpio_pull_down(pins[index]);
#endif
    }
    return true;
}

uint32_t fv_rp2354_input_pressed_mask(void) {
    uint32_t result = 0u;
    for (unsigned index = 0u; index < FV_INPUT_COUNT; ++index) {
        const bool high = gpio_get(pins[index]);
#if FUSE_VAULT_NAV_ACTIVE_LOW
        const bool pressed = !high;
#else
        const bool pressed = high;
#endif
        if (pressed) result |= FV_INPUT_BIT(index);
    }
    return result;
}
