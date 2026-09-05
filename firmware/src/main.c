#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_roots.h"
#include "fuse_vault/input.h"
#include "fuse_vault/journal_authenticator.h"
#include "fuse_vault/rp2354_input.h"
#include "fuse_vault/rp2354_otp.h"
#include "fuse_vault/rp2354_security_flash.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static fv_app_t app;
static fv_device_roots_storage_t device_roots_storage;
static fv_dual_journal_authenticator_t journal_authenticator;
static fv_journal_flash_t security_flash;
static fv_security_journal_t security_journal;
static fv_journal_state_t current_journal_state;
static bool journal_ready;
static fv_input_controller_t input_controller;

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool recover_active_device(fv_journal_state_t *journal_state) {
    fv_device_secret_t roots;
    if (fv_device_roots_read(&device_roots_storage, &roots) !=
        FV_DEVICE_ROOTS_ACTIVE) return false;
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    uint8_t device_context[FV_VAULT_ID_SIZE] = {'F', 'V', 'J', '1'};
    memcpy(device_context + 4u, board_id.id, sizeof(board_id.id));
    const bool authenticator_ready = fv_dual_journal_authenticator_init(
        &journal_authenticator, &roots, device_context);
    secure_clear(&roots, sizeof(roots));
    secure_clear(device_context, sizeof(device_context));
    if (!authenticator_ready ||
        !fv_security_journal_init(&security_journal, &security_flash,
                                  &journal_authenticator.interface)) {
        fv_dual_journal_authenticator_deinit(&journal_authenticator);
        return false;
    }
    if (fv_security_journal_recover(&security_journal, journal_state) !=
        FV_JOURNAL_OK) {
        fv_dual_journal_authenticator_deinit(&journal_authenticator);
        return false;
    }
    return true;
}

static void execute_commands(fv_command_set_t commands) {
    if ((commands & FV_COMMAND_STORE_ATTEMPT_COUNTER) != 0u) {
        if (!journal_ready || current_journal_state.sequence == UINT64_MAX) {
            execute_commands(fv_app_handle(&app, FV_EVENT_FATAL_ERROR));
            return;
        }
        fv_journal_state_t next = current_journal_state;
        next.previous_sequence = current_journal_state.sequence;
        ++next.sequence;
        next.failed_attempts = app.failed_attempts;
        if (fv_security_journal_append(&security_journal, &next) !=
            FV_JOURNAL_OK) {
            execute_commands(fv_app_handle(&app, FV_EVENT_FATAL_ERROR));
            return;
        }
        current_journal_state = next;
        execute_commands(fv_app_handle(&app,
                                       FV_EVENT_ATTEMPT_COUNTER_STORED));
    }
    if ((commands & FV_COMMAND_DESTROY_DEVICE_SECRET) != 0u) {
        const fv_device_roots_result_t result =
            fv_device_roots_revoke(&device_roots_storage);
        journal_ready = false;
        fv_dual_journal_authenticator_deinit(&journal_authenticator);
        if (result != FV_DEVICE_ROOTS_OK &&
            result != FV_DEVICE_ROOTS_REVOKED) {
            execute_commands(fv_app_handle(&app, FV_EVENT_FATAL_ERROR));
        }
    }
    /* Authentication and USB commands remain disconnected until those
       platform implementations exist. */
}

static void refresh_input_map(void) {
    fv_input_map_t map;
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&input_controller, &map);
}

static void handle_input_event(void *context, fv_event_t event) {
    (void)context;
    execute_commands(fv_app_handle(&app, event));
    refresh_input_map();
}

int main(void) {
    const bool input_ready = fv_rp2354_input_init();
    const bool security_flash_ready =
        fv_rp2354_security_flash_init(&security_flash);
    const bool device_roots_ready =
        fv_rp2354_otp_init(&device_roots_storage);
    const fv_device_roots_result_t roots_state = device_roots_ready
        ? fv_device_roots_status(&device_roots_storage)
        : FV_DEVICE_ROOTS_IO_ERROR;
    current_journal_state = (fv_journal_state_t){0};
    bool boot_storage_safe = security_flash_ready && device_roots_ready &&
                             input_ready;
    bool provisioned = false;
    uint8_t failed_attempts = 0u;
    fv_entry_method_t entry_method = FV_ENTRY_METHOD_WHEELS;
    if (boot_storage_safe && roots_state == FV_DEVICE_ROOTS_ACTIVE) {
        boot_storage_safe = recover_active_device(&current_journal_state) &&
                            current_journal_state.provisioned;
        if (boot_storage_safe) {
            provisioned = true;
            failed_attempts = current_journal_state.failed_attempts;
            journal_ready = true;

            /* The physical SD vault-header service is intentionally not
             * claimed yet. Provisioned hardware stays fail-closed until that
             * backend can be passed through this shared recovery boundary. */
            if (fv_boot_recover_entry_method(NULL, &entry_method) !=
                FV_BOOT_RECOVERY_OK) {
                boot_storage_safe = false;
                journal_ready = false;
            }
        }
    } else if (roots_state != FV_DEVICE_ROOTS_EMPTY) {
        boot_storage_safe = false;
    }
    fv_app_init(&app, provisioned, failed_attempts, entry_method);
    execute_commands(fv_app_handle(
        &app, boot_storage_safe ? FV_EVENT_BOOT_COMPLETED
                                : FV_EVENT_FATAL_ERROR));

    const fv_input_timing_t input_timing = {
        .debounce_ms = FV_INPUT_DEFAULT_DEBOUNCE_MS,
        .repeat_delay_ms = FV_INPUT_DEFAULT_REPEAT_DELAY_MS,
        .repeat_interval_ms = FV_INPUT_DEFAULT_REPEAT_INTERVAL_MS,
    };
    fv_input_controller_init(&input_controller, &input_timing,
                             fv_rp2354_input_pressed_mask(),
                             to_ms_since_boot(get_absolute_time()));
    refresh_input_map();

    for (;;) {
        fv_input_controller_update(
            &input_controller, fv_rp2354_input_pressed_mask(),
            to_ms_since_boot(get_absolute_time()), handle_input_event, NULL);
        sleep_ms(1u);
    }
}
