#include "fuse_vault/authentication_coordinator.h"

#include "fuse_vault/secret_input.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return false; } } while (0)

typedef struct {
    fv_device_secret_t roots;
    fv_vault_header_t header;
    fv_persist_result_t header_result;
    fv_persist_result_t roots_result;
    unsigned operation;
    unsigned header_operation;
    unsigned roots_operation;
} fixture_t;

static fv_persist_result_t load_header(fv_platform_services_t *services,
                                       fv_vault_header_t *header) {
    fixture_t *fixture = services->context;
    fixture->header_operation = ++fixture->operation;
    if (fixture->header_result == FV_PERSIST_OK) *header = fixture->header;
    return fixture->header_result;
}

static fv_persist_result_t read_roots(fv_platform_services_t *services,
                                      fv_device_secret_t *roots) {
    fixture_t *fixture = services->context;
    fixture->roots_operation = ++fixture->operation;
    if (fixture->roots_result == FV_PERSIST_OK) *roots = fixture->roots;
    return fixture->roots_result;
}

static bool random_fill(void *context, uint8_t *output, size_t length) {
    uint8_t *value = context;
    for (size_t index = 0u; index < length; ++index) output[index] = ++*value;
    return true;
}

static bool all_zero(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= bytes[index];
    return combined == 0u;
}

static fv_secret_entry_t entry_for(fv_entry_method_t method) {
    fv_secret_entry_t entry = {.method = method};
    switch (method) {
        case FV_ENTRY_METHOD_WHEELS:
            entry.state.wheels.values[0] = 12u;
            entry.state.wheels.values[1] = 34u;
            entry.state.wheels.values[2] = 56u;
            break;
        case FV_ENTRY_METHOD_DIRECTIONS:
            entry.state.directions.length = 6u;
            for (uint8_t i = 0u; i < 6u; ++i)
                entry.state.directions.values[i] = (uint8_t)(i % 4u);
            break;
        case FV_ENTRY_METHOD_KEYPAD:
            entry.state.keypad.length = 4u;
            entry.state.keypad.digits[0] = 1u;
            entry.state.keypad.digits[1] = 9u;
            entry.state.keypad.digits[2] = 8u;
            entry.state.keypad.digits[3] = 4u;
            break;
        case FV_ENTRY_METHOD_WORD_LIST:
            entry.state.word_list.words[0] = 3u;
            entry.state.word_list.words[1] = 17u;
            entry.state.word_list.words[2] = 31u;
            entry.state.word_list.words[3] = 63u;
            break;
        case FV_ENTRY_METHOD_COUNT: break;
    }
    return entry;
}

static bool prepare(fixture_t *fixture, fv_app_t *app,
                    fv_entry_method_t method, fv_volume_master_key_t *expected) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->header_result = FV_PERSIST_OK;
    fixture->roots_result = FV_PERSIST_OK;
    memset(&fixture->roots, 0x6bu, sizeof(fixture->roots));
    fixture->header.sequence = 1u;
    fixture->header.vault_id[0] = 1u;
    CHECK(fv_entry_method_to_secret_method(method,
                                            &fixture->header.entry_method));
    fv_secret_entry_t entry = entry_for(method);
    fv_secret_encoding_t encoding;
    CHECK(fv_secret_entry_encode(&entry, &encoding));
    uint8_t random_value = 0u;
    const fv_credential_costs_t costs = {1u, 1u};
    CHECK(fv_credential_envelope_create(
        &encoding, &fixture->roots, &costs, random_fill, &random_value,
        &fixture->header, expected) == FV_CREDENTIAL_OK);
    memset(&encoding, 0, sizeof(encoding));
    fv_app_init(app, true, 1u, method);
    app->state = FV_STATE_VAULT_AUTHENTICATING;
    app->secret_entry = entry;
    return true;
}

static const fv_platform_service_ops_t ops = {
    .read_device_secret = read_roots,
    .load_vault_header = load_header,
};

static bool success_across_methods(void) {
    for (fv_entry_method_t method = FV_ENTRY_METHOD_WHEELS;
         method < FV_ENTRY_METHOD_COUNT; ++method) {
        fixture_t fixture;
        fv_app_t app;
        fv_volume_master_key_t expected;
        CHECK(prepare(&fixture, &app, method, &expected));
        fv_platform_services_t services = {&ops, &fixture};
        fv_authentication_session_t session;
        fv_authentication_workspace_t workspace;
        memset(&session, 0xa5, sizeof(session));
        memset(&workspace, 0xa5, sizeof(workspace));
        CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
              FV_AUTHENTICATE_OK);
        CHECK(session.vmk_valid);
        CHECK(memcmp(&session.vmk, &expected, sizeof(expected)) == 0);
        CHECK(all_zero(&workspace, sizeof(workspace)));
        CHECK(fixture.header_operation < fixture.roots_operation);
        fv_authentication_session_clear(&session);
        CHECK(all_zero(&session, sizeof(session)));
    }
    return true;
}

static bool failure_cases(void) {
    fixture_t fixture;
    fv_app_t app;
    fv_volume_master_key_t expected;
    fv_platform_services_t services = {&ops, &fixture};
    fv_authentication_session_t session;
    fv_authentication_workspace_t workspace;

    CHECK(prepare(&fixture, &app, FV_ENTRY_METHOD_WHEELS, &expected));
    ++app.secret_entry.state.wheels.values[0];
    CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
          FV_AUTHENTICATE_REJECTED);
    CHECK(all_zero(&session, sizeof(session)));
    CHECK(all_zero(&workspace, sizeof(workspace)));

    CHECK(prepare(&fixture, &app, FV_ENTRY_METHOD_WHEELS, &expected));
    app.secret_entry.method = FV_ENTRY_METHOD_WORD_LIST;
    CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
          FV_AUTHENTICATE_REJECTED);
    CHECK(fixture.roots_operation == 0u);

    CHECK(prepare(&fixture, &app, FV_ENTRY_METHOD_WHEELS, &expected));
    fixture.header.wrapped_vmk_length = 0u;
    CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
          FV_AUTHENTICATE_FATAL);
    CHECK(fixture.roots_operation == 0u);

    CHECK(prepare(&fixture, &app, FV_ENTRY_METHOD_KEYPAD, &expected));
    fixture.roots_result = FV_PERSIST_IO_ERROR;
    CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
          FV_AUTHENTICATE_FATAL);
    CHECK(all_zero(&session, sizeof(session)));
    CHECK(all_zero(&workspace, sizeof(workspace)));

    CHECK(prepare(&fixture, &app, FV_ENTRY_METHOD_DIRECTIONS, &expected));
    app.state = FV_STATE_VAULT_RESERVING_ATTEMPT;
    CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
          FV_AUTHENTICATE_FATAL);
    CHECK(fixture.operation == 0u);
    return true;
}

static bool session_lifecycle_across_methods(void) {
    const fv_event_t teardown_events[] = {
        FV_EVENT_LOCK_REQUESTED,
        FV_EVENT_USB_EJECTED,
        FV_EVENT_STORAGE_FAILED,
    };
    for (fv_entry_method_t method = FV_ENTRY_METHOD_WHEELS;
         method < FV_ENTRY_METHOD_COUNT;
         method = (fv_entry_method_t)(method + 1)) {
        for (size_t teardown = 0u;
             teardown < sizeof(teardown_events) / sizeof(teardown_events[0]);
             ++teardown) {
            fixture_t fixture;
            fv_app_t app;
            fv_volume_master_key_t expected;
            CHECK(prepare(&fixture, &app, method, &expected));
            fv_platform_services_t services = {&ops, &fixture};
            fv_authentication_session_t session;
            fv_authentication_workspace_t workspace;
            CHECK(fv_authenticate(&app, &services, &session, &workspace) ==
                  FV_AUTHENTICATE_OK);
            CHECK(session.vmk_valid);

            fv_command_set_t commands = fv_app_handle(
                &app, FV_EVENT_AUTH_SUCCEEDED);
            CHECK((commands & FV_COMMAND_STORE_ATTEMPT_COUNTER) != 0u);
            CHECK((commands & FV_COMMAND_USB_ATTACH_MSC) == 0u);
            commands = fv_app_handle(&app, FV_EVENT_ATTEMPT_COUNTER_STORED);
            CHECK((commands & FV_COMMAND_USB_ATTACH_MSC) != 0u);
            CHECK(session.vmk_valid);

            commands = fv_app_handle(&app, teardown_events[teardown]);
            CHECK((commands & FV_COMMAND_ERASE_SESSION_KEYS) != 0u);
            fv_authentication_session_clear(&session);
            CHECK(all_zero(&session, sizeof(session)));
        }
    }

    fv_app_t app;
    fv_app_init(&app, true, FV_MAX_UNLOCK_ATTEMPTS,
                FV_ENTRY_METHOD_WHEELS);
    fv_authentication_session_t stale_session;
    memset(&stale_session, 0xa5, sizeof(stale_session));
    const fv_command_set_t commands = fv_app_handle(
        &app, FV_EVENT_BOOT_COMPLETED);
    CHECK((commands & FV_COMMAND_ERASE_SESSION_KEYS) != 0u);
    fv_authentication_session_clear(&stale_session);
    CHECK(all_zero(&stale_session, sizeof(stale_session)));
    return true;
}

int main(void) {
    if (!success_across_methods() || !failure_cases() ||
        !session_lifecycle_across_methods()) return 1;
    puts("All authentication-coordinator tests passed.");
    return 0;
}
