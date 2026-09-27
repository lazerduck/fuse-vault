#include "button_input.h"

void fv_button_input_init(fv_button_input *input) {
    *input = (fv_button_input){.armed = true};
}

void fv_button_input_require_release(fv_button_input *input, uint64_t now_us) {
    input->armed = false;
    input->changed_us = now_us;
}

unsigned fv_button_input_sample(fv_button_input *input, unsigned pressed, uint64_t now_us) {
    pressed &= FV_BUTTON_MASK;
    if (!pressed && input->candidate == pressed &&
        now_us - input->changed_us >= FV_BUTTON_DEBOUNCE_US)
        input->armed = true;
    if (pressed != input->candidate) {
        input->candidate = pressed;
        input->changed_us = now_us;
    }
    if (pressed == input->previous || now_us - input->changed_us < FV_BUTTON_DEBOUNCE_US)
        return 0;
    unsigned newly_pressed = pressed & ~input->previous;
    input->previous = pressed;
    if (input->armed && newly_pressed && !(newly_pressed & (newly_pressed - 1)))
        return newly_pressed;
    return 0;
}
