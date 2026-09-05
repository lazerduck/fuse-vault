#include "fuse_vault/input.h"

#include <stddef.h>
#include <string.h>

static bool time_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static void emit_binding(const fv_input_controller_t *controller,
                         fv_input_id_t input, fv_input_event_fn emit,
                         void *context) {
    const fv_input_binding_t *binding = &controller->map.bindings[input];
    if (binding->enabled && emit != NULL) emit(context, binding->event);
}

void fv_input_controller_init(fv_input_controller_t *controller,
                              const fv_input_timing_t *timing,
                              uint32_t initial_pressed_mask,
                              uint32_t now_ms) {
    if (controller == NULL || timing == NULL) return;
    memset(controller, 0, sizeof(*controller));
    controller->timing = *timing;
    for (unsigned index = 0u; index < FV_INPUT_COUNT; ++index) {
        const bool pressed =
            (initial_pressed_mask & FV_INPUT_BIT(index)) != 0u;
        controller->buttons[index] = (fv_input_button_state_t) {
            .sampled_pressed = pressed,
            .stable_pressed = pressed,
            .blocked_until_release = pressed,
            .sampled_since_ms = now_ms,
            .next_repeat_ms = now_ms + timing->repeat_delay_ms,
        };
    }
    controller->initialized = true;
}

void fv_input_controller_set_map(fv_input_controller_t *controller,
                                 const fv_input_map_t *map) {
    if (controller == NULL || map == NULL) return;
    bool map_changed = false;
    for (unsigned index = 0u; index < FV_INPUT_COUNT; ++index) {
        const fv_input_binding_t *old = &controller->map.bindings[index];
        const fv_input_binding_t *next = &map->bindings[index];
        if (old->enabled != next->enabled || old->event != next->event ||
            old->repeat != next->repeat) {
            map_changed = true;
        }
    }
    if (map_changed) {
        for (unsigned index = 0u; index < FV_INPUT_COUNT; ++index) {
            if (controller->buttons[index].stable_pressed) {
                controller->buttons[index].blocked_until_release = true;
            }
        }
    }
    controller->map = *map;
}

void fv_input_controller_update(fv_input_controller_t *controller,
                                uint32_t pressed_mask, uint32_t now_ms,
                                fv_input_event_fn emit, void *emit_context) {
    if (controller == NULL || !controller->initialized) return;
    for (unsigned index = 0u; index < FV_INPUT_COUNT; ++index) {
        fv_input_button_state_t *button = &controller->buttons[index];
        const bool pressed = (pressed_mask & FV_INPUT_BIT(index)) != 0u;
        if (pressed != button->sampled_pressed) {
            button->sampled_pressed = pressed;
            button->sampled_since_ms = now_ms;
        }
        if (button->stable_pressed != button->sampled_pressed &&
            time_reached(now_ms,
                         button->sampled_since_ms +
                             controller->timing.debounce_ms)) {
            button->stable_pressed = button->sampled_pressed;
            if (button->stable_pressed) {
                button->blocked_until_release = false;
                button->next_repeat_ms =
                    now_ms + controller->timing.repeat_delay_ms;
                emit_binding(controller, (fv_input_id_t)index, emit,
                             emit_context);
            } else {
                button->blocked_until_release = false;
            }
        } else if (button->stable_pressed &&
                   !button->blocked_until_release &&
                   controller->map.bindings[index].enabled &&
                   controller->map.bindings[index].repeat &&
                   controller->timing.repeat_interval_ms != 0u &&
                   time_reached(now_ms, button->next_repeat_ms)) {
            emit_binding(controller, (fv_input_id_t)index, emit, emit_context);
            do {
                button->next_repeat_ms +=
                    controller->timing.repeat_interval_ms;
            } while (time_reached(now_ms, button->next_repeat_ms));
        }
    }
}

void fv_input_map_clear(fv_input_map_t *map) {
    if (map != NULL) memset(map, 0, sizeof(*map));
}

bool fv_input_map_bind(fv_input_map_t *map, fv_input_id_t input,
                       fv_event_t event, bool repeat) {
    if (map == NULL || (unsigned)input >= FV_INPUT_COUNT ||
        (unsigned)event > FV_EVENT_FATAL_ERROR) {
        return false;
    }
    map->bindings[input] = (fv_input_binding_t) {
        .enabled = true,
        .repeat = repeat,
        .event = event,
    };
    return true;
}
