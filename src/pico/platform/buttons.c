#include "fuse_vault/pico_buttons.h"
#include "button_input.h"
#include "pico/stdlib.h"

static fv_button_input input;
static uint32_t prompt_generation;
#if FV_DEBUG_BUTTONS
static fv_button_diagnostics diagnostic;
static unsigned diagnostic_candidate;
static uint64_t diagnostic_changed[6];

fv_button_diagnostics fv_pico_buttons_diagnostics(void) {
    return diagnostic;
}
#endif
static const unsigned pins[] = {
    FUSE_VAULT_NAV_UP_PIN,    FUSE_VAULT_NAV_LEFT_PIN,   FUSE_VAULT_NAV_DOWN_PIN,
    FUSE_VAULT_NAV_RIGHT_PIN, FUSE_VAULT_NAV_SELECT_PIN, FUSE_VAULT_NAV_BACK_PIN,
};
static const fv_ui_key keys[] = {UI_UP, UI_LEFT, UI_DOWN, UI_RIGHT, UI_SELECT, UI_BACK};

void fv_pico_buttons_init(void) {
    fv_button_input_init(&input);
    prompt_generation = 0;
#if FV_DEBUG_BUTTONS
    diagnostic = (fv_button_diagnostics){0};
    diagnostic_candidate = 0;
    for (unsigned i = 0; i < 6; ++i)
        diagnostic_changed[i] = time_us_64();
#endif
    for (unsigned i = 0; i < 6; ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);
    }
}

fv_ui_key fv_pico_buttons_poll(uint32_t generation) {
    uint64_t now = time_us_64();
    if (prompt_generation != generation) {
        prompt_generation = generation;
        fv_button_input_require_release(&input, now);
    }
    unsigned gpio = gpio_get_all();
    unsigned sample = 0;
    for (unsigned i = 0; i < 6; ++i) {
        if (!(gpio & (1u << pins[i])))
            sample |= 1u << i;
    }
    unsigned pressed = fv_button_input_sample(&input, sample, now);
#if FV_DEBUG_BUTTONS
    /* Debounce independently so one faulty/noisy pin cannot hide the others.
     * Count every stable rising edge, including simultaneous presses. */
    diagnostic.raw_pressed = sample;
    for (unsigned i = 0; i < 6; ++i) {
        unsigned bit = 1u << i;
        if ((sample ^ diagnostic_candidate) & bit) {
            diagnostic_candidate ^= bit;
            diagnostic_changed[i] = now;
        }
        if (((sample ^ diagnostic.stable_pressed) & bit) &&
            now - diagnostic_changed[i] >= FV_BUTTON_DEBOUNCE_US) {
            diagnostic.stable_pressed ^= bit;
            if (sample & bit)
                ++diagnostic.presses[i];
        }
    }
#endif
    for (unsigned i = 0; i < 6; ++i) {
        if (pressed & (1u << i))
            return keys[i];
    }
    return 0;
}
