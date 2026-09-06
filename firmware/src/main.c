#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_roots.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/device_services.h"
#include "fuse_vault/display.h"
#include "fuse_vault/input.h"
#include "fuse_vault/fido_store.h"
#include "fuse_vault/fido_verification.h"
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

#if FUSE_VAULT_ENABLE_FIDO2
static fv_fido_store_t fido_store;
static fv_fido_verification_t fido_verification;
static bool fido_running, fido_fault, fido_reunlock;
static uint8_t fido_keepalive_status = 1;

static uint32_t fido_millis(void *context) {
    (void)context; return to_ms_since_boot(get_absolute_time());
}
static bool fido_safe(void *context) {
    (void)context;
    fv_rp2354_sd_poll(&sd);
    if (!runtime.authentication.vmk_valid || !app.session_unlocked ||
        !display_ready || !connector_ready ||
        !sd.interface.ops->is_present(&sd.interface) ||
        !fv_connector_safety_poll(&connector)) {
        fido_fault = true; return false;
    }
    if (fido_running && !fv_rp2354_usb_fido_poll(fido_millis(NULL), fido_keepalive_status)) return false;
    return true;
}
static void fido_presence_event(void *context, fv_event_t event) {
    int *result = context;
    if (event == FV_EVENT_SELECT) *result = 0;
    if (event == FV_EVENT_BACK) *result = 2;
}
static int fido_presence(void *context) {
    (void)context;
    fv_input_controller_t approval;
    fv_input_timing_t timing = {FV_INPUT_DEFAULT_DEBOUNCE_MS,
        FV_INPUT_DEFAULT_REPEAT_DELAY_MS, FV_INPUT_DEFAULT_REPEAT_INTERVAL_MS};
    uint32_t start = fido_millis(NULL);
    fv_input_controller_init(&approval, &timing, fv_rp2354_input_pressed_mask(), start);
    fv_input_map_t map;
    fv_input_map_clear(&map);
    (void)fv_input_map_bind(&map, FV_INPUT_SELECT, FV_EVENT_SELECT, false);
    (void)fv_input_map_bind(&map, FV_INPUT_BACK, FV_EVENT_BACK, false);
    fv_input_controller_set_map(&approval, &map);
    int result = -1;
    app.fido_waiting = true;
    fido_keepalive_status = 2;
    while (result < 0) {
        uint32_t now = fido_millis(NULL);
        if (!fido_safe(NULL)) { result = 2; break; }
        if ((uint32_t)(now - start) >= 30000u) { result = 1; break; }
        if (!fv_display_render(&display, &app, now)) {
            display_ready = false; fido_fault = true; result = 2; break;
        }
        fv_input_controller_update(&approval, fv_rp2354_input_pressed_mask(),
            now, fido_presence_event, &result);
        sleep_ms(1u);
    }
    app.fido_waiting = false;
    fido_keepalive_status = 1;
    /* Consume the approval press so it cannot become a menu action. */
    fv_input_controller_init(&input_controller, &timing,
        fv_rp2354_input_pressed_mask(), fido_millis(NULL));
    fv_input_map_for_app(&app, &map);
    fv_input_controller_set_map(&input_controller, &map);
    return result;
}
static bool fido_cancelled(void *context) {
    (void)context; return fido_fault || fv_rp2354_usb_fido_cancelled();
}
static uint8_t fido_uv_retries(void *context) {
    (void)context;
    return app.failed_attempts < FV_MAX_UNLOCK_ATTEMPTS ? (uint8_t)(FV_MAX_UNLOCK_ATTEMPTS - app.failed_attempts) : 0;
}
static bool fido_verify_user(void *context, const uint8_t *rp_hash) {
    (void)context;
    if (!runtime.authentication.vmk_valid || !app.session_unlocked ||
        !fv_fido_verification_use(&fido_verification, fido_millis(NULL), rp_hash)) {
        fido_reunlock = true; return false;
    }
    return true;
}
static size_t fido_dispatch(void *context, uint32_t channel,
    const uint8_t *request, size_t size, uint8_t *response, size_t capacity) {
    (void)context;
    fido_running = true;
    app.fido_reset_pending = size != 0 && request[0] == 7;
    size_t count = 1;
    response[0] = 0x7f;
    if (fido_safe(NULL)) count = fv_fido_engine_command_channel(channel,
        request, size, response, capacity);
    (void)fido_safe(NULL);
    if (!fido_store.ready || fido_fault) {
        response[0] = 0x7f; count = 1;
        if (!fido_fault && fv_rp2354_usb_fido_cancelled()) fido_reunlock = true;
        else fido_fault = true;
    }
    if (fido_verification.started && (uint32_t)(fido_millis(NULL) -
            fido_verification.verified_at) >= FV_FIDO_UV_COMPLETE_MS) {
        response[0] = 0x3c; count = 1; fido_reunlock = true;
    }
    fido_running = false;
    app.fido_reset_pending = false;
    return count;
}
#endif

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
#if FUSE_VAULT_ENABLE_FIDO2
    fv_fido_engine_close();
    fv_fido_store_close(&fido_store);
    fv_fido_verification_clear(&fido_verification);
#endif
    const bool route_ok = fv_connector_safety_disable(&connector);
    return usb_ok && route_ok;
}

static bool usb_attach_fido(void *context, const fv_volume_master_key_t *vmk,
    const fv_media_layout_t *layout, const fv_encryption_stack_descriptor_t *stack) {
    (void)context;
#if FUSE_VAULT_ENABLE_FIDO2
    fido_fault = false; fido_reunlock = false;
    if (!fido_safe(NULL) || !fv_fido_store_open(&fido_store, &services,
            &sd.interface, layout, vmk, stack)) return false;
    fido_store.progress = fido_safe;
    fv_fido_engine_ops_t ops = {.random = target_random, .commit = fv_fido_store_commit,
        .presence = fido_presence, .millis = fido_millis,
        .verify_user = fido_verify_user, .uv_retries = fido_uv_retries, .cancelled = fido_cancelled, .context = &fido_store};
    bool ok = fv_fido_engine_open(fido_store.image, fido_store.root_key,
        services_context.device_id, &ops) && fv_connector_safety_route(&connector) &&
        fv_rp2354_usb_fido_attach_engine(fido_dispatch, NULL);
    if (!ok) {
        (void)usb_detach(NULL);
        return false;
    }
    return true;
#else
    (void)vmk; (void)layout; (void)stack;
    return false;
#endif
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
#if FUSE_VAULT_ENABLE_FIDO2
    bool was_unlocked = app.session_unlocked;
#endif
    if (runtime_ready) {
        fv_device_runtime_handle_event(&runtime, event);
    } else {
        (void)fv_app_handle(&app, event);
    }
#if FUSE_VAULT_ENABLE_FIDO2
    if (!was_unlocked && app.session_unlocked)
        fv_fido_verification_begin(&fido_verification, fido_millis(NULL));
    if (!app.session_unlocked) fv_fido_verification_clear(&fido_verification);
#endif
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
#if FUSE_VAULT_ENABLE_FIDO2
    fv_app_set_fido_available(&app, true);
#endif

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
        fv_rp2354_usb_task_at(to_ms_since_boot(get_absolute_time()));
#if FUSE_VAULT_ENABLE_FIDO2
        if (app.state == FV_STATE_FIDO_READY && fido_verification.valid &&
            (uint32_t)(fido_millis(NULL) - fido_verification.verified_at) >= FV_FIDO_UV_COMPLETE_MS)
            fido_reunlock = true;
        if (fido_fault) {
            fido_fault = false;
            dispatch_event(FV_EVENT_STORAGE_FAILED);
        } else if (fido_reunlock) {
            fido_reunlock = false;
            dispatch_event(FV_EVENT_LOCK_REQUESTED);
        }
#endif
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
