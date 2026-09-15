#ifndef FUSE_VAULT_INPUT_H
#define FUSE_VAULT_INPUT_H

#include "fuse_vault/app.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FV_INPUT_UP = 0,
    FV_INPUT_DOWN,
    FV_INPUT_LEFT,
    FV_INPUT_RIGHT,
    FV_INPUT_SELECT,
    FV_INPUT_BACK,
    FV_INPUT_COUNT,
} fv_input_id_t;

#define FV_INPUT_BIT(input) (UINT32_C(1) << (unsigned)(input))

typedef struct {
    uint32_t debounce_ms;
    uint32_t repeat_delay_ms;
    uint32_t repeat_interval_ms;
} fv_input_timing_t;

#define FV_INPUT_DEFAULT_DEBOUNCE_MS 25u
#define FV_INPUT_DEFAULT_REPEAT_DELAY_MS 450u
#define FV_INPUT_DEFAULT_REPEAT_INTERVAL_MS 120u

typedef struct {
    bool enabled;
    bool repeat;
    fv_event_t event;
} fv_input_binding_t;

typedef struct {
    fv_input_binding_t bindings[FV_INPUT_COUNT];
} fv_input_map_t;

typedef struct {
    bool sampled_pressed;
    bool stable_pressed;
    bool blocked_until_release;
    uint32_t sampled_since_ms;
    uint32_t next_repeat_ms;
} fv_input_button_state_t;

typedef struct {
    fv_input_timing_t timing;
    fv_input_map_t map;
    fv_input_button_state_t buttons[FV_INPUT_COUNT];
    bool initialized;
} fv_input_controller_t;

typedef void (*fv_input_event_fn)(void *context, fv_event_t event);

void fv_input_controller_init(fv_input_controller_t *controller,
                              const fv_input_timing_t *timing,
                              uint32_t initial_pressed_mask,
                              uint32_t now_ms);
void fv_input_controller_set_map(fv_input_controller_t *controller,
                                 const fv_input_map_t *map);
void fv_input_controller_update(fv_input_controller_t *controller,
                                uint32_t pressed_mask, uint32_t now_ms,
                                fv_input_event_fn emit, void *emit_context);

void fv_input_map_clear(fv_input_map_t *map);
bool fv_input_map_bind(fv_input_map_t *map, fv_input_id_t input,
                       fv_event_t event, bool repeat);

/* Returns the controls accepted by the current screen and entry method. */
void fv_input_map_for_app(const fv_app_t *app, fv_input_map_t *map);

#endif
