#include "fuse_vault/device_runtime.h"

#include "fuse_vault/provisioning_coordinator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool random_adapter(void *context, uint8_t *output, size_t length) {
    fv_device_runtime_t *runtime = context;
    return runtime->services->ops->random_fill(runtime->services, output,
                                               length);
}

static void clear_session(fv_device_runtime_t *runtime) {
    if (runtime->encrypted_ready) {
        fv_encrypted_block_lock(&runtime->encrypted);
    }
    runtime->encrypted_ready = false;
    fv_authentication_session_clear(&runtime->authentication);
    clear(&runtime->media_layout, sizeof(runtime->media_layout));
    clear(&runtime->data_slice, sizeof(runtime->data_slice));
}

static bool prepare_encrypted_session(fv_device_runtime_t *runtime) {
    fv_device_secret_t roots = {0};
    fv_vault_header_t header = {0};
    const bool ok = runtime->authentication.vmk_valid &&
        runtime->services->ops->read_device_secret(runtime->services, &roots) ==
            FV_PERSIST_OK &&
        runtime->services->ops->load_vault_header(runtime->services, &header) ==
            FV_PERSIST_OK &&
        fv_media_load(runtime->raw_media, &roots, header.vault_id,
                      &runtime->media_layout) == FV_MEDIA_OK &&
        fv_media_open_data(&runtime->media_layout, runtime->raw_media,
                           &runtime->data_slice) &&
        fv_encrypted_block_init(
            &runtime->encrypted, &runtime->data_slice.interface,
            &runtime->authentication.vmk, header.vault_id,
            &header.encryption_stack, random_adapter, runtime);
    clear(&roots, sizeof(roots));
    clear(&header, sizeof(header));
    runtime->encrypted_ready = ok;
    if (!ok) clear_session(runtime);
    return ok;
}

static bool store_attempt_state(fv_device_runtime_t *runtime) {
    fv_security_state_t state;
    if (runtime->services->ops->load_security_state(runtime->services, &state) !=
            FV_PERSIST_OK ||
        state.sequence == UINT64_MAX) {
        return false;
    }
    state.sequence++;
    state.failed_attempts = runtime->app->failed_attempts;
    state.provisioned = runtime->app->provisioned;
    const bool ok = runtime->services->ops->store_security_state(
                        runtime->services, &state) == FV_PERSIST_OK;
    clear(&state, sizeof(state));
    return ok;
}

static fv_command_set_t fail(fv_device_runtime_t *runtime) {
    clear_session(runtime);
    return fv_app_handle(runtime->app,FV_EVENT_FATAL_ERROR);
}

bool fv_device_runtime_init(fv_device_runtime_t *runtime, fv_app_t *app,
                            fv_platform_services_t *services,
                            fv_block_device_t *raw_media,
                            const fv_runtime_usb_ops_t *usb_ops,
                            void *usb_context,
                            const fv_credential_costs_t *credential_costs) {
    if (runtime == NULL) return false;
    clear(runtime, sizeof(*runtime));
    if (app == NULL || services == NULL || services->ops == NULL ||
        services->ops->random_fill == NULL ||
        services->ops->device_secret_status == NULL ||
        services->ops->provision_device_secret == NULL ||
        services->ops->read_device_secret == NULL ||
        services->ops->revoke_device_secret == NULL ||
        services->ops->load_security_state == NULL ||
        services->ops->store_security_state == NULL ||
        services->ops->load_vault_header == NULL ||
        services->ops->store_vault_header == NULL || raw_media == NULL ||
        usb_ops == NULL || usb_ops->attach_msc == NULL ||
        usb_ops->detach_usb == NULL || credential_costs == NULL) {
        return false;
    }
    runtime->app = app;
    runtime->services = services;
    runtime->raw_media = raw_media;
    runtime->usb_ops = usb_ops;
    runtime->usb_context = usb_context;
    runtime->credential_costs = *credential_costs;
    return true;
}

void fv_device_runtime_execute(fv_device_runtime_t *runtime,
                               fv_command_set_t commands) {
    if (runtime == NULL || runtime->app == NULL) return;
    for (unsigned transitions = 0u;
         commands != FV_COMMAND_NONE && transitions < 16u; ++transitions) {
        fv_command_set_t next = FV_COMMAND_NONE;
        if ((commands & FV_COMMAND_USB_DETACH) != 0u) {
            if (!runtime->usb_ops->detach_usb(runtime->usb_context)) {
                next |= fail(runtime);
            }
        }
        if ((commands & FV_COMMAND_ERASE_TRANSIENT_SECRET) != 0u) {
            fv_secret_entry_clear(&runtime->app->secret_entry);
            fv_secret_entry_clear(&runtime->app->setup_secret_entry);
        }
        if ((commands & FV_COMMAND_ERASE_SESSION_KEYS) != 0u) {
            clear_session(runtime);
        }
        if ((commands & FV_COMMAND_DESTROY_DEVICE_SECRET) != 0u) {
            if (runtime->services->ops->revoke_device_secret(
                    runtime->services) != FV_PERSIST_OK) {
                next |= fail(runtime);
            }
        }
        if ((commands & FV_COMMAND_INSPECT_MEDIA) != 0u) {
            const fv_media_result_t result =
                fv_media_classify(runtime->raw_media);
            next |= fv_app_handle(
                runtime->app,
                result == FV_MEDIA_ABSENT || result == FV_MEDIA_IO_ERROR
                    ? FV_EVENT_MEDIA_FAILED : FV_EVENT_MEDIA_FOUND);
        }
        if ((commands & FV_COMMAND_PREPARE_MEDIA) != 0u) {
            const fv_media_result_t result =
                fv_media_prepare_for_initialization(runtime->raw_media);
            next |= fv_app_handle(
                runtime->app, result == FV_MEDIA_OK
                    ? FV_EVENT_MEDIA_PREPARED : FV_EVENT_MEDIA_FAILED);
        }
        if ((commands & FV_COMMAND_BEGIN_PROVISIONING) != 0u) {
            fv_setup_provision_workspace_t workspace;
            const fv_setup_provision_result_t result = fv_setup_provision(
                runtime->app, runtime->services, &runtime->credential_costs,
                &workspace);
            next |= fv_app_handle(
                runtime->app, result == FV_SETUP_PROVISION_OK
                                  ? FV_EVENT_PROVISIONING_SUCCEEDED
                                  : FV_EVENT_PROVISIONING_FAILED);
        }
        if ((commands & FV_COMMAND_STORE_ATTEMPT_COUNTER) != 0u) {
            next |= store_attempt_state(runtime)
                ? fv_app_handle(runtime->app,
                                FV_EVENT_ATTEMPT_COUNTER_STORED)
                : fail(runtime);
        }
        if ((commands & FV_COMMAND_BEGIN_AUTHENTICATION) != 0u) {
            fv_authentication_workspace_t workspace;
            const fv_authenticate_result_t result = fv_authenticate(
                runtime->app, runtime->services, &runtime->authentication,
                &workspace);
            if (result == FV_AUTHENTICATE_OK &&
                prepare_encrypted_session(runtime)) {
                next |= fv_app_handle(runtime->app, FV_EVENT_AUTH_SUCCEEDED);
            } else if (result == FV_AUTHENTICATE_REJECTED) {
                next |= fv_app_handle(runtime->app, FV_EVENT_AUTH_FAILED);
            } else {
                next |= fail(runtime);
            }
        }
        if ((commands & FV_COMMAND_USB_ATTACH_MSC) != 0u) {
            if (!runtime->encrypted_ready ||
                !runtime->usb_ops->attach_msc(
                    runtime->usb_context, &runtime->encrypted.interface)) {
                next |= fail(runtime);
            }
        }
        if ((commands & FV_COMMAND_USB_ATTACH_FIDO) != 0u) {
            if (!runtime->authentication.vmk_valid ||
                !runtime->encrypted_ready || !runtime->app->session_unlocked ||
                runtime->usb_ops->attach_fido == NULL ||
                !runtime->usb_ops->attach_fido(runtime->usb_context,
                    &runtime->authentication.vmk, &runtime->media_layout,
                    &runtime->encrypted.pipeline.descriptor)) {
                next |= fail(runtime);
            }
        }
        commands = next;
    }
    if (commands != FV_COMMAND_NONE) (void)fail(runtime);
}

void fv_device_runtime_handle_event(fv_device_runtime_t *runtime,
                                    fv_event_t event) {
    if (runtime != NULL) {
        fv_device_runtime_execute(runtime,
                                  fv_app_handle(runtime->app, event));
    }
}

void fv_device_runtime_shutdown(fv_device_runtime_t *runtime) {
    if (runtime == NULL) return;
    if (runtime->usb_ops != NULL && runtime->usb_ops->detach_usb != NULL) {
        (void)runtime->usb_ops->detach_usb(runtime->usb_context);
    }
    clear_session(runtime);
    clear(runtime, sizeof(*runtime));
}
