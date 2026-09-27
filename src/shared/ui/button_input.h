#ifndef FV_BUTTON_INPUT_H
#define FV_BUTTON_INPUT_H
#include <stdbool.h>
#include <stdint.h>

#define FV_BUTTON_DEBOUNCE_US 25000u
#define FV_BUTTON_MASK 0x3fu

typedef struct {
    unsigned previous, candidate;
    uint64_t changed_us;
    bool armed;
} fv_button_input;

void fv_button_input_init(fv_button_input *);
/* Used for a new approval prompt: a held button must not approve the next one. */
void fv_button_input_require_release(fv_button_input *, uint64_t now_us);
/* Bits are active-high logical button states, independent of GPIO polarity.
 * Returns one newly pressed bit after stable debounce, or zero. Chords are ignored. */
unsigned fv_button_input_sample(fv_button_input *, unsigned pressed, uint64_t now_us);
#endif
