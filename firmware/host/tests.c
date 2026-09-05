#include "fuse_vault/app.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS);
    CHECK(fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_MODE_SELECT);
    return app;
}

static fv_command_set_t reserve_and_begin_authentication(fv_app_t *app) {
    fv_command_set_t commands = fv_app_handle(app, FV_EVENT_SELECT);
    CHECK(has_command(commands, FV_COMMAND_STORE_ATTEMPT_COUNTER));
    CHECK(!has_command(commands, FV_COMMAND_BEGIN_AUTHENTICATION));
    CHECK(app->state == FV_STATE_VAULT_RESERVING_ATTEMPT);

    commands = fv_app_handle(app, FV_EVENT_ATTEMPT_COUNTER_STORED);
    CHECK(has_command(commands, FV_COMMAND_BEGIN_AUTHENTICATION));
    CHECK(app->state == FV_STATE_VAULT_AUTHENTICATING);
    return commands;
}

static void test_boot_paths(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    CHECK(app.state == FV_STATE_BOOTING);
    CHECK(fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_SETUP_REQUIRED);

    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_SETUP_METHOD_SELECT);
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_SETUP_SECRET_ENTRY);

    (void)fv_app_handle(&app, FV_EVENT_UP);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_SETUP_SECRET_CONFIRM);
    (void)fv_app_handle(&app, FV_EVENT_UP);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_SETUP_POLICY_CONFIRM);

    fv_secret_encoding_t setup_encoding;
    CHECK(fv_setup_secret_encode(&app, &setup_encoding));
    CHECK(setup_encoding.bytes[5] == 1u);

    CHECK(has_command(fv_app_handle(&app, FV_EVENT_SELECT),
                      FV_COMMAND_BEGIN_PROVISIONING));
    CHECK(app.state == FV_STATE_PROVISIONING);
    CHECK(has_command(fv_app_handle(&app, FV_EVENT_PROVISIONING_SUCCEEDED),
                      FV_COMMAND_ERASE_TRANSIENT_SECRET));
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(app.provisioned);

    fv_app_init(&app, true, FV_MAX_UNLOCK_ATTEMPTS, FV_ENTRY_METHOD_WHEELS);
    const fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    CHECK(app.state == FV_STATE_DESTROYED);
    CHECK(has_command(commands, FV_COMMAND_DESTROY_DEVICE_SECRET));
}

static void test_setup_rejects_mismatched_confirmation(void) {
    fv_app_t app;
    fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);

    (void)fv_app_handle(&app, FV_EVENT_UP);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_SETUP_SECRET_CONFIRM);

    (void)fv_app_handle(&app, FV_EVENT_DOWN);
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_SETUP_SECRET_MISMATCH);
    CHECK(app.secret_entry.state.wheels.values[0] == 0u);
    CHECK(app.setup_secret_entry.state.wheels.values[0] == 0u);

    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_SETUP_SECRET_ENTRY);
}

static void test_vault_unlock_and_lock(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);
    CHECK(app.state == FV_STATE_VAULT_SECRET_ENTRY);

    CHECK(fv_app_handle(&app, FV_EVENT_UP) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.values[0] == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_DOWN) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.values[0] == 0u);
    CHECK(fv_app_handle(&app, FV_EVENT_DOWN) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.values[0] == 99u);
    CHECK(fv_app_handle(&app, FV_EVENT_RIGHT) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.selected == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_UP) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.values[1] == 1u);
    CHECK(fv_app_handle(&app, FV_EVENT_LEFT) == FV_COMMAND_NONE);
    CHECK(app.secret_entry.state.wheels.selected == 0u);

    fv_command_set_t commands = reserve_and_begin_authentication(&app);
    CHECK(!has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
    CHECK(app.state == FV_STATE_VAULT_AUTHENTICATING);

    commands = fv_app_handle(&app, FV_EVENT_AUTH_SUCCEEDED);
    CHECK(app.secret_entry.state.wheels.values[0] == 0u);
    CHECK(app.secret_entry.state.wheels.values[1] == 0u);
    CHECK(app.secret_entry.state.wheels.values[2] == 0u);
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
    CHECK(app.secret_entry.state.wheels.values[0] == 1u);
    CHECK(app.secret_entry.state.wheels.values[1] == 99u);

    CHECK(has_command(fv_app_handle(&app, FV_EVENT_BACK),
                      FV_COMMAND_ERASE_TRANSIENT_SECRET));
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(app.secret_entry.state.wheels.values[0] == 0u);
    CHECK(app.secret_entry.state.wheels.values[1] == 0u);
    CHECK(app.secret_entry.state.wheels.selected == 0u);
}

static void test_secret_encoding_is_versioned_and_stable(void) {
    fv_app_t app = boot_provisioned();
    (void)fv_app_handle(&app, FV_EVENT_SELECT);
    app.secret_entry.state.wheels.values[0] = 12u;
    app.secret_entry.state.wheels.values[1] = 34u;
    app.secret_entry.state.wheels.values[2] = 56u;

    fv_secret_encoding_t encoding;
    CHECK(fv_secret_input_encode(&app, &encoding));
    const uint8_t expected[] = {
        0x46u, 0x56u, 0x02u, 0x01u, 0x03u, 12u, 34u, 56u,
    };
    for (size_t index = 0u; index < sizeof(expected); ++index) {
        CHECK(encoding.bytes[index] == expected[index]);
    }
}

static void test_all_modular_entry_methods(void) {
    fv_secret_entry_t entry;
    fv_secret_encoding_t encoding;

    fv_secret_entry_begin(&entry, FV_ENTRY_METHOD_DIRECTIONS);
    CHECK(fv_secret_entry_handle(&entry, FV_EVENT_SELECT) == FV_SECRET_EVENT_IGNORED);
    const fv_event_t directions[] = {
        FV_EVENT_UP, FV_EVENT_RIGHT, FV_EVENT_DOWN,
        FV_EVENT_LEFT, FV_EVENT_UP, FV_EVENT_RIGHT,
    };
    for (size_t index = 0u; index < sizeof(directions) / sizeof(directions[0]); ++index) {
        CHECK(fv_secret_entry_handle(&entry, directions[index]) == FV_SECRET_EVENT_CHANGED);
    }
    CHECK(fv_secret_entry_handle(&entry, FV_EVENT_SELECT) == FV_SECRET_EVENT_COMPLETE);
    CHECK(fv_secret_entry_encode(&entry, &encoding));
    CHECK(encoding.bytes[3] == FV_SECRET_METHOD_DIRECTIONS_V1);
    CHECK(encoding.bytes[4] == 6u);

    fv_secret_entry_begin(&entry, FV_ENTRY_METHOD_KEYPAD);
    for (unsigned digit = 0u; digit < 4u; ++digit) {
        CHECK(fv_secret_entry_handle(&entry, FV_EVENT_SELECT) == FV_SECRET_EVENT_CHANGED);
    }
    CHECK(entry.state.keypad.length == 4u);
    (void)fv_secret_entry_handle(&entry, FV_EVENT_LEFT);
    (void)fv_secret_entry_handle(&entry, FV_EVENT_UP);
    CHECK(entry.state.keypad.selected == 11u);
    CHECK(fv_secret_entry_handle(&entry, FV_EVENT_SELECT) == FV_SECRET_EVENT_COMPLETE);
    CHECK(fv_secret_entry_encode(&entry, &encoding));
    CHECK(encoding.bytes[3] == FV_SECRET_METHOD_KEYPAD_V1);
    CHECK(encoding.bytes[4] == 4u);

    fv_secret_entry_begin(&entry, FV_ENTRY_METHOD_WORD_LIST);
    (void)fv_secret_entry_handle(&entry, FV_EVENT_UP);
    (void)fv_secret_entry_handle(&entry, FV_EVENT_RIGHT);
    (void)fv_secret_entry_handle(&entry, FV_EVENT_DOWN);
    CHECK(fv_secret_entry_handle(&entry, FV_EVENT_SELECT) == FV_SECRET_EVENT_COMPLETE);
    CHECK(fv_secret_entry_encode(&entry, &encoding));
    CHECK(encoding.bytes[3] == FV_SECRET_METHOD_WORD_LIST_V1);
    CHECK(encoding.bytes[4] == FV_SECRET_WORD_COUNT);
    CHECK(encoding.bytes[5] == 1u);
    CHECK(encoding.bytes[6] == 63u);

    fv_secret_entry_clear(&entry);
    const uint8_t *bytes = (const uint8_t *)&entry;
    for (size_t index = 0u; index < sizeof(entry); ++index) CHECK(bytes[index] == 0u);
}

static void enter_test_secret(fv_app_t *app, fv_entry_method_t method) {
    if (method == FV_ENTRY_METHOD_DIRECTIONS) {
        const fv_event_t sequence[] = {
            FV_EVENT_UP, FV_EVENT_RIGHT, FV_EVENT_DOWN,
            FV_EVENT_LEFT, FV_EVENT_UP, FV_EVENT_RIGHT, FV_EVENT_SELECT,
        };
        for (size_t index = 0u; index < sizeof(sequence) / sizeof(sequence[0]); ++index)
            (void)fv_app_handle(app, sequence[index]);
    } else if (method == FV_ENTRY_METHOD_KEYPAD) {
        for (unsigned digit = 0u; digit < 4u; ++digit)
            (void)fv_app_handle(app, FV_EVENT_SELECT);
        (void)fv_app_handle(app, FV_EVENT_LEFT);
        (void)fv_app_handle(app, FV_EVENT_UP);
        (void)fv_app_handle(app, FV_EVENT_SELECT);
    } else if (method == FV_ENTRY_METHOD_WORD_LIST) {
        for (unsigned slot = 0u; slot < FV_SECRET_WORD_COUNT; ++slot) {
            (void)fv_app_handle(app, FV_EVENT_UP);
            if (slot + 1u < FV_SECRET_WORD_COUNT)
                (void)fv_app_handle(app, FV_EVENT_RIGHT);
        }
        (void)fv_app_handle(app, FV_EVENT_SELECT);
    }
}

static void test_new_methods_work_in_setup_and_unlock(void) {
    for (fv_entry_method_t method = FV_ENTRY_METHOD_DIRECTIONS;
         method < FV_ENTRY_METHOD_COUNT;
         method = (fv_entry_method_t)(method + 1)) {
        fv_app_t setup;
        fv_app_init(&setup, false, 0u, FV_ENTRY_METHOD_WHEELS);
        (void)fv_app_handle(&setup, FV_EVENT_BOOT_COMPLETED);
        (void)fv_app_handle(&setup, FV_EVENT_SELECT);
        for (fv_entry_method_t selected = FV_ENTRY_METHOD_WHEELS;
             selected < method; selected = (fv_entry_method_t)(selected + 1))
            (void)fv_app_handle(&setup, FV_EVENT_DOWN);
        CHECK(setup.selected_entry_method == method);
        (void)fv_app_handle(&setup, FV_EVENT_SELECT);
        enter_test_secret(&setup, method);
        CHECK(setup.state == FV_STATE_SETUP_SECRET_CONFIRM);
        enter_test_secret(&setup, method);
        CHECK(setup.state == FV_STATE_SETUP_POLICY_CONFIRM);

        fv_app_t unlock;
        fv_app_init(&unlock, true, 0u, method);
        (void)fv_app_handle(&unlock, FV_EVENT_BOOT_COMPLETED);
        (void)fv_app_handle(&unlock, FV_EVENT_SELECT);
        CHECK(unlock.secret_entry.method == method);
        enter_test_secret(&unlock, method);
        CHECK(unlock.state == FV_STATE_VAULT_RESERVING_ATTEMPT);
    }
}

static void test_ui_framebuffer_is_deterministic(void) {
    fv_app_t app = boot_provisioned();
    fv_framebuffer_t first;
    fv_framebuffer_t second;
    fv_ui_draw(&app, &first);
    fv_ui_draw(&app, &second);
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
}

static void test_attempt_limit_destroys_secret(void) {
    fv_app_t app = boot_provisioned();
    CHECK(fv_app_handle(&app, FV_EVENT_SELECT) == FV_COMMAND_NONE);

    for (unsigned attempt = 1u; attempt <= FV_MAX_UNLOCK_ATTEMPTS; ++attempt) {
        (void)reserve_and_begin_authentication(&app);
        const fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_AUTH_FAILED);
        CHECK(!has_command(commands, FV_COMMAND_USB_ATTACH_MSC));
        CHECK(has_command(commands, FV_COMMAND_ERASE_TRANSIENT_SECRET));
        CHECK(!has_command(commands, FV_COMMAND_STORE_ATTEMPT_COUNTER));

        if (attempt < FV_MAX_UNLOCK_ATTEMPTS) {
            CHECK(app.state == FV_STATE_VAULT_SECRET_ENTRY);
            CHECK(!has_command(commands, FV_COMMAND_DESTROY_DEVICE_SECRET));
        } else {
            CHECK(app.state == FV_STATE_DESTROYED);
            CHECK(has_command(commands, FV_COMMAND_DESTROY_DEVICE_SECRET));
            CHECK(has_command(commands, FV_COMMAND_USB_DETACH));
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
    (void)reserve_and_begin_authentication(&app);
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
    test_setup_rejects_mismatched_confirmation();
    test_vault_unlock_and_lock();
    test_secret_entry_back_clears_input();
    test_secret_encoding_is_versioned_and_stable();
    test_all_modular_entry_methods();
    test_new_methods_work_in_setup_and_unlock();
    test_ui_framebuffer_is_deterministic();
    test_attempt_limit_destroys_secret();
    test_fido_mode();
    test_fault_closes_security_boundary();
    test_usb_attachment_invariants();
    puts("All Fuse Vault core tests passed.");
    return EXIT_SUCCESS;
}
