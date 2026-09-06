#include "fuse_vault/provisioning_coordinator.h"
#include "fuse_vault/authentication_coordinator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef enum {
    FAIL_NONE = 0,
    FAIL_STATUS,
    FAIL_RANDOM,
    FAIL_ENVELOPE_RANDOM,
    FAIL_PROVISION_ROOTS,
    FAIL_PROVISION_ROOTS_AFTER_COMMIT,
    FAIL_READ_ROOTS,
    FAIL_STORE_HEADER,
    FAIL_STORE_HEADER_AFTER_COMMIT,
    FAIL_LOAD_HEADER,
    CORRUPT_HEADER,
    FAIL_STORE_STATE,
    FAIL_STORE_STATE_AFTER_COMMIT,
    FAIL_LOAD_STATE_AFTER_STORE,
    CORRUPT_STATE,
} failure_t;

typedef struct {
    failure_t failure;
    uint8_t random_value;
    bool roots_active;
    bool revoked;
    bool header_present;
    bool state_present;
    fv_device_secret_t roots;
    fv_vault_header_t header;
    fv_security_state_t state;
    char stores[4];
    size_t store_count;
    size_t random_calls;
} fixture_t;

static fixture_t *fixture(fv_platform_services_t *services) {
    return services->context;
}

static bool random_fill(fv_platform_services_t *services, uint8_t *output,
                        size_t length) {
    fixture_t *state = fixture(services);
    ++state->random_calls;
    if (state->failure == FAIL_RANDOM ||
        (state->failure == FAIL_ENVELOPE_RANDOM && state->random_calls == 3u)) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        output[index] = ++state->random_value;
    }
    return true;
}

static fv_persist_result_t device_secret_status(
    fv_platform_services_t *services, fv_device_secret_status_t *status) {
    if (fixture(services)->failure == FAIL_STATUS) return FV_PERSIST_IO_ERROR;
    *status = fixture(services)->roots_active ? FV_DEVICE_SECRET_ACTIVE
                                             : FV_DEVICE_SECRET_EMPTY;
    return FV_PERSIST_OK;
}

static fv_persist_result_t provision_device_secret(
    fv_platform_services_t *services, const fv_device_secret_t *roots) {
    fixture_t *state = fixture(services);
    if (state->failure == FAIL_PROVISION_ROOTS) return FV_PERSIST_IO_ERROR;
    state->roots = *roots;
    state->roots_active = true;
    if (state->failure == FAIL_PROVISION_ROOTS_AFTER_COMMIT) {
        return FV_PERSIST_IO_ERROR;
    }
    return FV_PERSIST_OK;
}

static fv_persist_result_t read_device_secret(
    fv_platform_services_t *services, fv_device_secret_t *roots) {
    fixture_t *state = fixture(services);
    if (state->failure == FAIL_READ_ROOTS) return FV_PERSIST_IO_ERROR;
    *roots = state->roots;
    return FV_PERSIST_OK;
}

static fv_persist_result_t revoke_device_secret(
    fv_platform_services_t *services) {
    fixture_t *state = fixture(services);
    state->revoked = true;
    state->roots_active = false;
    memset(&state->roots, 0, sizeof(state->roots));
    return FV_PERSIST_OK;
}

static fv_persist_result_t load_security_state(
    fv_platform_services_t *services, fv_security_state_t *output) {
    fixture_t *state = fixture(services);
    if (!state->state_present) return FV_PERSIST_NOT_FOUND;
    if (state->failure == FAIL_LOAD_STATE_AFTER_STORE) return FV_PERSIST_IO_ERROR;
    *output = state->state;
    if (state->failure == CORRUPT_STATE) ++output->sequence;
    return FV_PERSIST_OK;
}

static fv_persist_result_t store_security_state(
    fv_platform_services_t *services, const fv_security_state_t *input) {
    fixture_t *state = fixture(services);
    if (state->failure == FAIL_STORE_STATE) return FV_PERSIST_IO_ERROR;
    state->stores[state->store_count++] = 'S';
    state->state = *input;
    state->state_present = true;
    return state->failure == FAIL_STORE_STATE_AFTER_COMMIT
        ? FV_PERSIST_IO_ERROR : FV_PERSIST_OK;
}

static fv_persist_result_t load_vault_header(
    fv_platform_services_t *services, fv_vault_header_t *output) {
    fixture_t *state = fixture(services);
    if (!state->header_present) return FV_PERSIST_NOT_FOUND;
    if (state->failure == FAIL_LOAD_HEADER) return FV_PERSIST_IO_ERROR;
    *output = state->header;
    if (state->failure == CORRUPT_HEADER) output->wrapped_vmk[0] ^= 1u;
    return FV_PERSIST_OK;
}

static fv_persist_result_t store_vault_header(
    fv_platform_services_t *services, const fv_vault_header_t *input) {
    fixture_t *state = fixture(services);
    if (state->failure == FAIL_STORE_HEADER) return FV_PERSIST_IO_ERROR;
    state->stores[state->store_count++] = 'H';
    state->header = *input;
    state->header_present = true;
    return state->failure == FAIL_STORE_HEADER_AFTER_COMMIT
        ? FV_PERSIST_IO_ERROR : FV_PERSIST_OK;
}

static const fv_platform_service_ops_t OPS = {
    .random_fill = random_fill,
    .device_secret_status = device_secret_status,
    .provision_device_secret = provision_device_secret,
    .read_device_secret = read_device_secret,
    .revoke_device_secret = revoke_device_secret,
    .load_security_state = load_security_state,
    .store_security_state = store_security_state,
    .load_vault_header = load_vault_header,
    .store_vault_header = store_vault_header,
};

static fv_app_t provisioning_app(fv_entry_method_t method) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, method);
    app.state = FV_STATE_PROVISIONING;
    app.setup_secret_entry.method = method;
    if (method == FV_ENTRY_METHOD_WHEELS) {
        app.setup_secret_entry.state.wheels.values[0] = 12u;
        app.setup_secret_entry.state.wheels.values[1] = 34u;
        app.setup_secret_entry.state.wheels.values[2] = 56u;
    } else if (method == FV_ENTRY_METHOD_DIRECTIONS) {
        app.setup_secret_entry.state.directions.length = 6u;
        const uint8_t values[6] = {0u, 1u, 2u, 3u, 0u, 1u};
        memcpy(app.setup_secret_entry.state.directions.values, values,
               sizeof(values));
    } else if (method == FV_ENTRY_METHOD_KEYPAD) {
        app.setup_secret_entry.state.keypad.length = 4u;
        app.setup_secret_entry.state.keypad.digits[0] = 1u;
        app.setup_secret_entry.state.keypad.digits[1] = 9u;
        app.setup_secret_entry.state.keypad.digits[2] = 8u;
        app.setup_secret_entry.state.keypad.digits[3] = 4u;
    } else if (method == FV_ENTRY_METHOD_WORD_LIST) {
        app.setup_secret_entry.state.word_list.words[0] = 3u;
        app.setup_secret_entry.state.word_list.words[1] = 17u;
        app.setup_secret_entry.state.word_list.words[2] = 31u;
        app.setup_secret_entry.state.word_list.words[3] = 63u;
    }
    return app;
}

static bool workspace_is_clear(const fv_setup_provision_workspace_t *workspace) {
    const uint8_t *bytes = (const uint8_t *)workspace;
    uint8_t combined = 0u;
    for (size_t index = 0u; index < sizeof(*workspace); ++index) {
        combined |= bytes[index];
    }
    return combined == 0u;
}

static bool bytes_are_clear(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= bytes[index];
    return combined == 0u;
}

static fv_setup_provision_result_t run(failure_t failure, fixture_t *state,
                                       fv_setup_provision_workspace_t *workspace) {
    *state = (fixture_t) {.failure = failure};
    memset(workspace, 0xa5, sizeof(*workspace));
    fv_platform_services_t services = {.ops = &OPS, .context = state};
    fv_app_t app = provisioning_app(FV_ENTRY_METHOD_DIRECTIONS);
    const fv_credential_costs_t costs = {
        .pbkdf2_iterations = 1u,
        .kmac_iterations = 1u,
    };
    return fv_setup_provision(&app, &services, &costs, workspace);
}

static void test_success(void) {
    for (fv_entry_method_t method = FV_ENTRY_METHOD_WHEELS;
         method < FV_ENTRY_METHOD_COUNT;
         method = (fv_entry_method_t)(method + 1)) {
        fixture_t state = {0};
        fv_setup_provision_workspace_t workspace;
        fv_platform_services_t services = {.ops = &OPS, .context = &state};
        fv_app_t app = provisioning_app(method);
        const fv_credential_costs_t costs = {1u, 1u};
        CHECK(fv_setup_provision(&app, &services, &costs, &workspace) ==
              FV_SETUP_PROVISION_OK);
        CHECK(workspace_is_clear(&workspace));
        CHECK(state.roots_active && !state.revoked);
        CHECK(state.header_present && state.state_present);
        fv_entry_method_t recovered;
        CHECK(fv_secret_method_to_entry_method(state.header.entry_method,
                                                &recovered));
        CHECK(recovered == method);
        CHECK(state.header.sequence == 1u);
        CHECK(state.state.provisioned && state.state.sequence == 1u);
        CHECK(state.store_count == 2u && state.stores[0] == 'H' &&
              state.stores[1] == 'S');

        /* A fresh app object models restart after the publish-last commit. */
        fv_app_t restarted;
        fv_app_init(&restarted, state.state.provisioned,
                    state.state.failed_attempts, recovered);
        CHECK(fv_app_handle(&restarted, FV_EVENT_BOOT_COMPLETED) ==
              FV_COMMAND_NONE);
        CHECK(restarted.state == FV_STATE_MODE_SELECT);
        (void)fv_app_handle(&restarted, FV_EVENT_SELECT);
        restarted.secret_entry = app.setup_secret_entry;
        restarted.failed_attempts = 1u;
        restarted.state = FV_STATE_VAULT_AUTHENTICATING;
        fv_authentication_session_t session;
        fv_authentication_workspace_t auth_workspace;
        CHECK(fv_authenticate(&restarted, &services, &session,
                              &auth_workspace) == FV_AUTHENTICATE_OK);
        CHECK(session.vmk_valid);
        CHECK((fv_app_handle(&restarted, FV_EVENT_AUTH_SUCCEEDED) &
               FV_COMMAND_STORE_ATTEMPT_COUNTER) != 0u);
        CHECK((fv_app_handle(&restarted, FV_EVENT_ATTEMPT_COUNTER_STORED) &
               FV_COMMAND_USB_ATTACH_MSC) != 0u);
        const fv_command_set_t lock_commands =
            fv_app_handle(&restarted, FV_EVENT_LOCK_REQUESTED);
        CHECK((lock_commands & FV_COMMAND_ERASE_SESSION_KEYS) != 0u);
        fv_authentication_session_clear(&session);
        CHECK(bytes_are_clear(&session, sizeof(session)));
    }
}

static void test_failures_preserve_device_roots(void) {
    const failure_t failures[] = {
        FAIL_READ_ROOTS, FAIL_STORE_HEADER, FAIL_STORE_HEADER_AFTER_COMMIT,
        FAIL_LOAD_HEADER, CORRUPT_HEADER, FAIL_STORE_STATE,
        FAIL_STORE_STATE_AFTER_COMMIT, FAIL_LOAD_STATE_AFTER_STORE, CORRUPT_STATE,
        FAIL_ENVELOPE_RANDOM,
    };
    for (size_t index = 0u; index < sizeof(failures) / sizeof(failures[0]);
         ++index) {
        fixture_t state;
        fv_setup_provision_workspace_t workspace;
        CHECK(run(failures[index], &state, &workspace) !=
              FV_SETUP_PROVISION_OK);
        CHECK(workspace_is_clear(&workspace));
        CHECK(!state.revoked && state.roots_active);
    }
}

static void test_lost_root_commit_acknowledgement_is_verified(void) {
    fixture_t state;
    fv_setup_provision_workspace_t workspace;
    CHECK(run(FAIL_PROVISION_ROOTS_AFTER_COMMIT, &state, &workspace) ==
          FV_SETUP_PROVISION_OK);
    CHECK(workspace_is_clear(&workspace));
    CHECK(state.roots_active && !state.revoked);
    CHECK(state.header_present && state.state_present);
}

static void test_factory_provisioned_roots_are_reused(void) {
    fixture_t state = {.roots_active = true};
    for (size_t index = 0u; index < sizeof(state.roots.device_secret); ++index) {
        state.roots.device_secret[index] = (uint8_t)(index + 1u);
    }
    const fv_device_secret_t expected = state.roots;
    fv_platform_services_t services = {.ops = &OPS, .context = &state};
    fv_app_t app = provisioning_app(FV_ENTRY_METHOD_WHEELS);
    const fv_credential_costs_t costs = {1u, 1u};
    fv_setup_provision_workspace_t workspace;
    CHECK(fv_setup_provision(&app, &services, &costs, &workspace) ==
          FV_SETUP_PROVISION_OK);
    CHECK(memcmp(&state.roots, &expected, sizeof(expected)) == 0);
    CHECK(state.roots_active && !state.revoked);
    CHECK(state.header_present && state.state_present);
}

static void test_early_failures_do_not_mutate_storage(void) {
    const failure_t failures[] = {
        FAIL_STATUS, FAIL_RANDOM, FAIL_PROVISION_ROOTS,
    };
    for (size_t index = 0u; index < sizeof(failures) / sizeof(failures[0]);
         ++index) {
        fixture_t state;
        fv_setup_provision_workspace_t workspace;
        CHECK(run(failures[index], &state, &workspace) !=
              FV_SETUP_PROVISION_OK);
        CHECK(workspace_is_clear(&workspace));
        CHECK(!state.revoked && !state.roots_active);
        CHECK(!state.header_present && !state.state_present);
    }
}

static void test_rejects_stale_material(void) {
    fixture_t state = {.header_present = true};
    fv_platform_services_t services = {.ops = &OPS, .context = &state};
    fv_app_t app = provisioning_app(FV_ENTRY_METHOD_DIRECTIONS);
    const fv_credential_costs_t costs = {1u, 1u};
    fv_setup_provision_workspace_t workspace;
    memset(&workspace, 0xa5, sizeof(workspace));
    CHECK(fv_setup_provision(&app, &services, &costs, &workspace) ==
          FV_SETUP_PROVISION_NOT_PRISTINE);
    CHECK(workspace_is_clear(&workspace));
    CHECK(!state.roots_active && !state.revoked && !state.state_present);
}

static void test_rejects_invalid_cost_before_storage(void) {
    fixture_t state = {0};
    fv_platform_services_t services = {.ops = &OPS, .context = &state};
    fv_app_t app = provisioning_app(FV_ENTRY_METHOD_DIRECTIONS);
    const fv_credential_costs_t costs = {0u, 1u};
    fv_setup_provision_workspace_t workspace;
    memset(&workspace, 0xa5, sizeof(workspace));
    CHECK(fv_setup_provision(&app, &services, &costs, &workspace) ==
          FV_SETUP_PROVISION_INVALID_ARGUMENT);
    CHECK(workspace_is_clear(&workspace));
    CHECK(state.random_calls == 0u && !state.roots_active && !state.revoked);
}

int main(void) {
    test_success();
    test_failures_preserve_device_roots();
    test_lost_root_commit_acknowledgement_is_verified();
    test_factory_provisioned_roots_are_reused();
    test_early_failures_do_not_mutate_storage();
    test_rejects_stale_material();
    test_rejects_invalid_cost_before_storage();
    puts("All provisioning-coordinator tests passed.");
    return EXIT_SUCCESS;
}
