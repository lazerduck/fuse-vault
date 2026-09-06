#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_roots.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/device_services.h"
#include "fuse_vault/display.h"
#include "fuse_vault/input.h"
#include "fuse_vault/peripheral_safety.h"
#include "fuse_vault/rp2354_connector.h"
#include "fuse_vault/rp2354_input.h"
#include "fuse_vault/rp2354_display.h"
#include "fuse_vault/rp2354_otp.h"
#include "fuse_vault/rp2354_random.h"
#include "fuse_vault/rp2354_sd.h"
#include "fuse_vault/rp2354_security_flash.h"
#include "fuse_vault/rp2354_usb_msc.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static fv_app_t app;
static fv_device_roots_storage_t roots_storage;
static fv_journal_flash_t security_flash;
static fv_rp2354_sd_t sd;
static fv_rp2354_connector_t connector_context;
static fv_connector_safety_t connector;
static fv_platform_services_t services;
static fv_device_services_context_t services_context;
static fv_device_runtime_t runtime;
static fv_input_controller_t input_controller;
static fv_display_t display;
static fv_rp2354_display_t display_context;
static bool runtime_ready;
static bool display_ready;
static bool connector_ready;

typedef enum {
    BOOT_RECOVERY_READY = 0,
    BOOT_RECOVERY_WAITING_FOR_MEDIA,
    BOOT_RECOVERY_FAILED,
} boot_recovery_state_t;

static bool target_random(void *context, uint8_t *output, size_t length) {
    (void)context;
    return fv_rp2354_random_fill(output, length);
}

static bool usb_attach_msc(void *context,
                           fv_block_device_t *plaintext_blocks) {
    (void)context;
    if (!fv_connector_safety_poll(&connector) ||
        !fv_connector_safety_route(&connector)) {
        return false;
    }
    if (fv_rp2354_usb_msc_attach(plaintext_blocks)) return true;
    (void)fv_connector_safety_disable(&connector);
    return false;
}

static bool usb_detach(void *context) {
    (void)context;
    const bool usb_ok = fv_rp2354_usb_msc_detach();
    const bool route_ok = fv_connector_safety_disable(&connector);
    return usb_ok && route_ok;
}

static bool usb_attach_fido(void *context) {
    (void)context;
    /* CTAP2 has an independent reserved media/key domain, but is not a V1
     * storage-release feature and must not be represented as implemented. */
    return false;
}

static const fv_runtime_usb_ops_t USB_OPS = {
    .attach_msc = usb_attach_msc,
    .detach_usb = usb_detach,
    .attach_fido = usb_attach_fido,
};

static void refresh_input_map(void) {
    fv_input_map_t map;
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&input_controller, &map);
}

static void dispatch_event(fv_event_t event) {
    if (runtime_ready) {
        fv_device_runtime_handle_event(&runtime, event);
    } else {
        (void)fv_app_handle(&app, event);
    }
    if (display_ready && !fv_display_render(
            &display, &app, to_ms_since_boot(get_absolute_time()))) {
        display_ready = false;
        if (runtime_ready && app.state != FV_STATE_FAULT) {
            fv_device_runtime_handle_event(&runtime, FV_EVENT_FATAL_ERROR);
        }
    }
    refresh_input_map();
}

static void handle_input_event(void *context, fv_event_t event) {
    (void)context;
    dispatch_event(event);
}

static boot_recovery_state_t recover_boot_state(
    bool *provisioned, uint8_t *failed_attempts,
    fv_entry_method_t *entry_method) {
    fv_device_secret_status_t roots_status = FV_DEVICE_SECRET_INVALID;
    if (services.ops->device_secret_status(&services, &roots_status) !=
        FV_PERSIST_OK) {
        return BOOT_RECOVERY_FAILED;
    }
    if (roots_status == FV_DEVICE_SECRET_REVOKED ||
        roots_status == FV_DEVICE_SECRET_INVALID) {
        return BOOT_RECOVERY_FAILED;
    }
    if (roots_status == FV_DEVICE_SECRET_EMPTY) {
        /* First setup creates and verifies roots on a fresh device using this
         * same image. Invalid, revoked and unreadable OTP fails above. */
        return roots_storage.root_programming_enabled
            ? BOOT_RECOVERY_READY : BOOT_RECOVERY_FAILED;
    }

    fv_security_state_t state;
    const fv_persist_result_t state_result =
        services.ops->load_security_state(&services, &state);
    if (state_result == FV_PERSIST_NOT_FOUND) return BOOT_RECOVERY_READY;
    if (state_result != FV_PERSIST_OK || !state.provisioned) {
        return BOOT_RECOVERY_FAILED;
    }
    *provisioned = true;
    *failed_attempts = state.failed_attempts;
    if (sd.interface.ops == NULL || sd.interface.ops->is_present == NULL ||
        !sd.interface.ops->is_present(&sd.interface)) {
        return BOOT_RECOVERY_WAITING_FOR_MEDIA;
    }
    return fv_boot_recover_entry_method(&services, entry_method) ==
               FV_BOOT_RECOVERY_OK
        ? BOOT_RECOVERY_READY : BOOT_RECOVERY_FAILED;
}

int main(void) {
    /* OE# is pulled low on PCB revision 1 so USB-C is available to the ROM
     * loader. Disconnect the data mux before initializing any other
     * application peripheral. */
    fv_rp2354_connector_init(&connector_context);
    connector_ready = fv_connector_safety_init(
        &connector, &fv_rp2354_connector_ops, &connector_context, NULL, NULL);
    const bool input_ready = fv_rp2354_input_init();
    const bool flash_ready =
        fv_rp2354_security_flash_init(&security_flash);
    const bool roots_ready = fv_rp2354_otp_init(&roots_storage);
    const bool sd_driver_ready = fv_rp2354_sd_init(&sd);
    const bool usb_ready = fv_rp2354_usb_msc_init();
    display_ready = fv_display_init(&display, &fv_rp2354_display_ops,
                                    &display_context, 33u);

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    uint8_t device_id[FV_VAULT_ID_SIZE] = {
        'F','V','R','P','2','3','5','4'
    };
    memcpy(device_id + 8u, board_id.id, sizeof(board_id.id));
    const bool services_ready = flash_ready && roots_ready && sd_driver_ready &&
        fv_device_services_init(
            &services, &services_context, &roots_storage, &security_flash,
            &sd.interface, device_id, target_random, NULL);
    memset(device_id, 0, sizeof(device_id));

    bool provisioned = false;
    uint8_t failed_attempts = 0u;
    fv_entry_method_t entry_method = FV_ENTRY_METHOD_WHEELS;
    const boot_recovery_state_t recovery = services_ready
        ? recover_boot_state(&provisioned, &failed_attempts, &entry_method)
        : BOOT_RECOVERY_FAILED;
    const bool boot_safe = input_ready && display_ready && usb_ready &&
        connector_ready && services_ready;
    fv_app_init(&app, provisioned, failed_attempts, entry_method);

    const fv_credential_costs_t credential_costs = {
        /* Initial target values; the release ceremony must calibrate these
         * against measured device latency and raise them where practical. */
        .pbkdf2_iterations = 100000u,
        .kmac_iterations = 10000u,
    };
    runtime_ready = boot_safe && fv_device_runtime_init(
        &runtime, &app, &services, &sd.interface, &USB_OPS, NULL,
        &credential_costs);
    if (runtime_ready && recovery == BOOT_RECOVERY_READY) {
        fv_device_runtime_handle_event(&runtime, FV_EVENT_BOOT_COMPLETED);
    } else if (runtime_ready &&
               recovery == BOOT_RECOVERY_WAITING_FOR_MEDIA) {
        fv_device_runtime_handle_event(&runtime,
                                       FV_EVENT_BOOT_MEDIA_REQUIRED);
    } else {
        (void)fv_app_handle(&app, FV_EVENT_FATAL_ERROR);
        (void)fv_rp2354_usb_msc_detach();
    }
    if (display_ready) {
        display_ready = fv_display_render(
            &display, &app, to_ms_since_boot(get_absolute_time()));
    }

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
        fv_rp2354_sd_poll(&sd);
        if (runtime_ready && fv_rp2354_sd_take_insertion_event(&sd) &&
            app.state == FV_STATE_BOOT_MEDIA_REQUIRED) {
            bool recovered_provisioned = false;
            uint8_t recovered_attempts = 0u;
            fv_entry_method_t recovered_method = FV_ENTRY_METHOD_WHEELS;
            const boot_recovery_state_t inserted_recovery = recover_boot_state(
                &recovered_provisioned, &recovered_attempts,
                &recovered_method);
            if (inserted_recovery == BOOT_RECOVERY_READY &&
                recovered_provisioned) {
                app.failed_attempts = recovered_attempts;
                app.selected_entry_method = recovered_method;
                dispatch_event(FV_EVENT_BOOT_COMPLETED);
            } else if (inserted_recovery == BOOT_RECOVERY_FAILED) {
                dispatch_event(FV_EVENT_FATAL_ERROR);
            }
        }
        if (runtime_ready && fv_rp2354_sd_take_removal_event(&sd)) {
            dispatch_event(FV_EVENT_STORAGE_FAILED);
        }
        if (runtime_ready && fv_rp2354_sd_take_failure_event(&sd)) {
            dispatch_event(FV_EVENT_STORAGE_FAILED);
        }
        if (runtime_ready && connector_ready &&
            !fv_connector_safety_poll(&connector)) {
            connector_ready = false;
            dispatch_event(FV_EVENT_FATAL_ERROR);
        }
        /* Check physical removal/conflict before accepting another host
         * transaction, then let TinyUSB produce any logical eject event. */
        fv_rp2354_usb_msc_task();
        if (runtime_ready && fv_rp2354_usb_msc_take_storage_failure()) {
            dispatch_event(FV_EVENT_STORAGE_FAILED);
        }
        if (runtime_ready && fv_rp2354_usb_msc_take_eject_request()) {
            dispatch_event(FV_EVENT_USB_EJECTED);
        }
        fv_input_controller_update(
            &input_controller, fv_rp2354_input_pressed_mask(),
            to_ms_since_boot(get_absolute_time()), handle_input_event, NULL);
        /* Retry rate-limited frames even when no further input arrives. */
        if (display_ready && !fv_display_render(
                &display, &app, to_ms_since_boot(get_absolute_time()))) {
            display_ready = false;
            if (runtime_ready) dispatch_event(FV_EVENT_FATAL_ERROR);
        }
        sleep_ms(1u);
    }
}
