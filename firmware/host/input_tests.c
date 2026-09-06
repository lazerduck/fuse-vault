#include "fuse_vault/input.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    fv_event_t events[16];
    size_t count;
} event_log_t;

static void record_event(void *context, fv_event_t event) {
    event_log_t *log = context;
    CHECK(log->count < sizeof(log->events) / sizeof(log->events[0]));
    log->events[log->count++] = event;
}

static void update(fv_input_controller_t *controller, uint32_t mask,
                   uint32_t time_ms, event_log_t *log) {
    fv_input_controller_update(controller, mask, time_ms, record_event, log);
}

static fv_input_controller_t controller_for(const fv_app_t *app) {
    const fv_input_timing_t timing = {
        .debounce_ms = 20u,
        .repeat_delay_ms = 100u,
        .repeat_interval_ms = 40u,
    };
    fv_input_controller_t controller;
    fv_input_controller_init(&controller, &timing, 0u, 0u);
    fv_input_map_t map;
    fv_input_map_for_app(app, &map);
    fv_input_controller_set_map(&controller, &map);
    return controller;
}

static void test_debounce_edges_and_non_repeat(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    fv_input_controller_t controller = controller_for(&app);
    event_log_t log = {0};
    const uint32_t select = FV_INPUT_BIT(FV_INPUT_SELECT);
    update(&controller, select, 1u, &log);
    update(&controller, 0u, 10u, &log); /* contact bounce */
    update(&controller, select, 12u, &log);
    update(&controller, select, 31u, &log);
    CHECK(log.count == 0u);
    update(&controller, select, 32u, &log);
    CHECK(log.count == 1u && log.events[0] == FV_EVENT_SELECT);
    update(&controller, select, 1000u, &log);
    CHECK(log.count == 1u);
    update(&controller, 0u, 1001u, &log);
    update(&controller, 0u, 1021u, &log);
    update(&controller, select, 1022u, &log);
    update(&controller, select, 1042u, &log);
    CHECK(log.count == 2u);
}

static void test_repeat_and_state_bindings(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    app.state = FV_STATE_SETUP_METHOD_SELECT;
    fv_input_controller_t controller = controller_for(&app);
    event_log_t log = {0};
    const uint32_t up = FV_INPUT_BIT(FV_INPUT_UP);
    update(&controller, up, 1u, &log);
    update(&controller, up, 21u, &log);
    CHECK(log.count == 1u && log.events[0] == FV_EVENT_UP);
    update(&controller, up, 120u, &log);
    CHECK(log.count == 1u);
    update(&controller, up, 121u, &log);
    CHECK(log.count == 2u && log.events[1] == FV_EVENT_UP);
    update(&controller, up, 241u, &log);
    CHECK(log.count == 3u); /* missed intervals coalesce into one event */

    app.state = FV_STATE_PROVISIONING;
    fv_input_map_t map;
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&controller, &map);
    update(&controller, up, 1000u, &log);
    CHECK(log.count == 3u);
}

static void test_direction_sequences_do_not_repeat(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_DIRECTIONS);
    app.state = FV_STATE_SETUP_SECRET_ENTRY;
    app.selected_entry_method = FV_ENTRY_METHOD_DIRECTIONS;
    fv_input_controller_t controller = controller_for(&app);
    event_log_t log = {0};
    const uint32_t right = FV_INPUT_BIT(FV_INPUT_RIGHT);
    update(&controller, right, 1u, &log);
    update(&controller, right, 21u, &log);
    update(&controller, right, 1000u, &log);
    CHECK(log.count == 1u && log.events[0] == FV_EVENT_RIGHT);
}

static void test_word_picker_requires_separate_presses(void) {
    const fv_state_t states[] = {FV_STATE_SETUP_SECRET_ENTRY,
        FV_STATE_SETUP_SECRET_CONFIRM, FV_STATE_VAULT_SECRET_ENTRY};
    for (unsigned state = 0u; state < 3u; ++state) {
        fv_app_t app;
        fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WORD_LIST);
        app.state = states[state];
        app.selected_entry_method = FV_ENTRY_METHOD_WORD_LIST;
        for (unsigned button = 0u; button < FV_INPUT_COUNT; ++button) {
            fv_input_controller_t controller = controller_for(&app);
            event_log_t log = {0};
            uint32_t mask = FV_INPUT_BIT(button);
            update(&controller, mask, 1u, &log);
            update(&controller, mask, 21u, &log);
            update(&controller, mask, 1000u, &log);
            CHECK(log.count == 1u);
            update(&controller, 0u, 1001u, &log);
            update(&controller, 0u, 1021u, &log);
            update(&controller, mask, 1022u, &log);
            update(&controller, mask, 1042u, &log);
            CHECK(log.count == 2u);
        }
    }
}

static void test_changed_binding_requires_release(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    app.state = FV_STATE_SETUP_REQUIRED;
    fv_input_controller_t controller = controller_for(&app);
    event_log_t log = {0};
    const uint32_t up = FV_INPUT_BIT(FV_INPUT_UP);
    update(&controller, up, 1u, &log);
    update(&controller, up, 21u, &log); /* stable but disabled */
    CHECK(log.count == 0u);
    app.state = FV_STATE_SETUP_METHOD_SELECT;
    fv_input_map_t map;
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&controller, &map);
    update(&controller, up, 1000u, &log);
    CHECK(log.count == 0u);
    update(&controller, 0u, 1001u, &log);
    update(&controller, 0u, 1021u, &log);
    update(&controller, up, 1022u, &log);
    update(&controller, up, 1042u, &log);
    CHECK(log.count == 1u && log.events[0] == FV_EVENT_UP);
}

static void test_screen_change_blocks_unchanged_held_binding(void) {
    fv_app_t app;
    fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS);
    app.state = FV_STATE_MODE_SELECT;
    fv_input_controller_t controller = controller_for(&app);
    event_log_t log = {0};
    const uint32_t up = FV_INPUT_BIT(FV_INPUT_UP);
    update(&controller, up, 1u, &log);
    update(&controller, up, 21u, &log);
    CHECK(log.count == 1u);

    /* Up has the same event/repeat settings, but the screen map changes. */
    app.state = FV_STATE_VAULT_SECRET_ENTRY;
    fv_input_map_t map;
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&controller, &map);
    update(&controller, up, 1000u, &log);
    CHECK(log.count == 1u);
}

int main(void) {
    test_debounce_edges_and_non_repeat();
    test_repeat_and_state_bindings();
    test_direction_sequences_do_not_repeat();
    test_word_picker_requires_separate_presses();
    test_changed_binding_requires_release();
    test_screen_change_blocks_unchanged_held_binding();
    puts("All input-controller tests passed.");
    return EXIT_SUCCESS;
}
