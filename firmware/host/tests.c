#include "fuse_vault/app.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void check(bool condition, const char *expression, const char *file, int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

static bool has_command(fv_command_set_t commands, fv_command_t command) {
    return (commands & (fv_command_set_t)command) != 0u;
}

static fv_app_t boot_provisioned(void) {
    fv_app_t app;
    fv_app_init(&app, true, 0u);
    CHECK(fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_MODE_SELECT);
    return app;
}

static void test_boot_paths(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u);
    CHECK(app.state == FV_STATE_BOOTING);
    CHECK(fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_SETUP_REQUIRED);

    CHECK(has_command(fv_app_handle(&app, FV_EVENT_SELECT),
                      FV_COMMAND_BEGIN_PROVISIONING));
    CHECK(app.state == FV_STATE_PROVISIONING);
    CHECK(has_command(fv_app_handle(&app, FV_EVENT_PROVISIONING_SUCCEEDED),
                      FV_COMMAND_ERASE_TRANSIENT_SECRET));
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(app.provisioned);

    fv_app_init(&app, true, FV_MAX_UNLOCK_ATTEMPTS);
    const fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    CHECK(app.state == FV_STATE_DESTROYED);
    CHECK(has_command(commands, FV_COMMAND_DESTROY_DEVICE_SECRET));
}

static void test_vault_unlock_and_lock(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_VAULT_SECRET_ENTRY);

    CHECK(fv_app_handle(&app, FV_EVENT_UP) == FV_COMMAND_NONE);
    CHECK(app.secret_wheels[0] == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_DOWN) == FV_COMMAND_NONE);
    CHECK(app.secret_wheels[0] == 0u);
    CHECK(fv_app_handle(&app, FV_EVENT_DOWN) == FV_COMMAND_NONE);
    CHECK(app.secret_wheels[0] == 99u);
    CHECK(fv_app_handle(&app, FV_EVENT_RIGHT) == FV_COMMAND_NONE);
    CHECK(app.selected_secret_wheel == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_UP) == FV_COMMAND_NONE);
    CHECK(app.secret_wheels[1] == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_LEFT) == FV_COMMAND_NONE);
    CHECK(app.selected_secret_wheel == 0u);

    fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(has_command(commands, FV_COMMAND_BEGIN_AUTHENTICATION));
    CHECK(!has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
    CHECK(app.state == FV_STATE_VAULT_AUTHENTICATING);

    commands = fv_app_handle(&app, FV_EVENT_AUTH_SUCCEEDED);
    CHECK(app.secret_wheels[0] == 0u);
    CHECK(app.secret_wheels[1] == 0u);
    CHECK(app.secret_wheels[2] == 0u);
    CHECK(!has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
    CHECK(has_command(commands, FV_COMMAND_ERASE_TRANSIENT_SECRET));
    CHECK(has_command(commands, FV_COMMAND_STORE_ATTEMPT_COUNTER));
    CHECK(app.state == FV_STATE_VAULT_RECORDING_SUCCESS);

    commands = fv_app_handle(&app, FV_EVENT_ATTEMPT_COUNTER_STORED);
    CHECK(has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
    CHECK(app.state == FV_STATE_VAULT_UNLOCKED);

    commands = fv_app_handle(&app, FV_EVENT_LOCK_REQUESTED);
    CHECK(has_command(commands, FV_COMMAND_USB_DETACH));
    CHECK(has_command(commands, FV_COMMAND_ERASE_SESSION_KEYS));
    CHECK(app.state == FV_STATE_MODE_SELECT);
}

static void test_secret_entry_back_clears_input(void) {
    fv_app_t app = boot_provisioned();
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    (void)fv_app_handle(&app, FV_EVENT_UP);
    (void)fv_app_handle(&app, FV_EVENT_RIGHT);
    (void)fv_app_handle(&app, FV_EVENT_DOWN);
    CHECK(app.secret_wheels[0] == 1u);
    CHECK(app.secret_wheels[1] == 99u);

    CHECK(has_command(fv_app_handle(&app, FV_EVENT_BACK),
                      FV_COMMAND_ERASE_TRANSIENT_SECRET));
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(app.secret_wheels[0] == 0u);
    CHECK(app.secret_wheels[1] == 0u);
    CHECK(app.selected_secret_wheel == 0u);
}

static void test_attempt_limit_destroys_secret(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);

    for (unsigned attempt = 1u; attempt <= FV_MAX_UNLOCK_ATTEMPTS; ++attempt) {
        CHECK(has_command(fv_app_handle(&app, FV_EVENT_SELECT),
                          FV_COMMAND_BEGIN_AUTHENTICATION));
        const fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_AUTH_FAILED);
        CHECK(!has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
        CHECK(has_command(commands, FV_COMMAND_ERASE_TRANSIENT_SECRET));
        CHECK(has_command(commands, FV_COMMAND_STORE_ATTEMPT_COUNTER));
        CHECK(app.state == FV_STATE_VAULT_RECORDING_FAILURE);

        const fv_command_set_t stored = fv_app_handle(
            &app, FV_EVENT_ATTEMPT_COUNTER_STORED);

        if (attempt < FV_MAX_UNLOCK_ATTEMPTS) {
            CHECK(app.state == FV_STATE_VAULT_SECRET_ENTRY);
            CHECK(!has_command(stored, FV_COMMAND_DESTROY_DEVICE_SECRET));
        } else {
            CHECK(app.state == FV_STATE_DESTROYED);
            CHECK(has_command(stored, FV_COMMAND_DESTROY_DEVICE_SECRET));
            CHECK(has_command(stored, FV_COMMAND_USB_DETACH));
        }
    }
}

static void test_fido_mode(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_DOWN) == FV_COMMAND_NONE);
    CHECK(app.selected_mode == FV_MODE_FIDO);

    fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_FIDO_READY);
    CHECK(has_command(commands, FV_COMMAND_USB_ATTACH_FIDO));

    commands = fv_app_handle(&app, FV_EVENT_BACK);
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(has_command(commands, FV_COMMAND_USB_DETACH));
}

static void test_fault_closes_security_boundary(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);
    CHECK(has_command(fv_app_handle(&app, FV_EVENT_SELECT),
                      FV_COMMAND_BEGIN_AUTHENTICATION));
    CHECK(has_command(fv_app_handle(&app, FV_EVENT_AUTH_SUCCEEDED),
                      FV_COMMAND_STORE_ATTEMPT_COUNTER));
    CHECK(has_command(fv_app_handle(&app, FV_EVENT_ATTEMPT_COUNTER_STORED),
                      FV_COMMAND_USB_ATTACH_MSC));

    const fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_STORAGE_FAILED);
    CHECK(app.state == FV_STATE_FAULT);
    CHECK(has_command(commands, FV_COMMAND_USB_DETACH));
    CHECK(has_command(commands, FV_COMMAND_ERASE_SESSION_KEYS));
}

static void test_usb_attachment_invariants(void) {
    for (unsigned state = 0u; state <= (unsigned)FV_STATE_FAULT; ++state) {
        for (unsigned event = 0u; event <= (unsigned)FV_EVENT_FATAL_ERROR; ++event) {
            fv_app_t app = {
                .state = (fv_state_t)state,
                .selected_mode = FV_MODE_VAULT,
                .failed_attempts = 0u,
                .provisioned = true,
            };
            const fv_command_set_t commands = fv_app_handle(&app, (fv_event_t)event);
            if (has_command(commands, FV_COMMAND_USB_ATTACH_MSC)) {
                CHECK(state == (unsigned)FV_STATE_VAULT_RECORDING_SUCCESS);
                CHECK(event == (unsigned)FV_EVENT_ATTEMPT_COUNTER_STORED);
            }
        }
    }
}

int main(void) {
    test_boot_paths();
    test_vault_unlock_and_lock();
    test_secret_entry_back_clears_input();
    test_attempt_limit_destroys_secret();
    test_fido_mode();
    test_fault_closes_security_boundary();
    test_usb_attachment_invariants();
    puts("All Fuse Vault core tests passed.");
    return EXIT_SUCCESS;
}
