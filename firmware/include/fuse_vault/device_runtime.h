#ifndef FUSE_VAULT_DEVICE_RUNTIME_H
#define FUSE_VAULT_DEVICE_RUNTIME_H

#include "fuse_vault/app.h"
#include "fuse_vault/authentication_coordinator.h"
#include "fuse_vault/block_slice.h"
#include "fuse_vault/encrypted_block.h"
#include "fuse_vault/media_layout.h"
#include "fuse_vault/persistence.h"

typedef struct fv_device_runtime fv_device_runtime_t;

typedef struct {
    bool (*attach_msc)(void *context, fv_block_device_t *plaintext_blocks);
    bool (*detach_usb)(void *context);
    bool (*attach_fido)(void *context);
} fv_runtime_usb_ops_t;

struct fv_device_runtime {
    fv_app_t *app;
    fv_platform_services_t *services;
    fv_block_device_t *raw_media;
    const fv_runtime_usb_ops_t *usb_ops;
    void *usb_context;
    fv_credential_costs_t credential_costs;
    fv_authentication_session_t authentication;
    fv_encrypted_block_t encrypted;
    fv_media_layout_t media_layout;
    fv_block_slice_t data_slice;
    bool encrypted_ready;
};

bool fv_device_runtime_init(fv_device_runtime_t *runtime, fv_app_t *app,
                            fv_platform_services_t *services,
                            fv_block_device_t *raw_media,
                            const fv_runtime_usb_ops_t *usb_ops,
                            void *usb_context,
                            const fv_credential_costs_t *credential_costs);

/* Runs an application event and all resulting platform commands to a stable
 * state. Any ambiguous platform failure drives the application to FAULT. */
void fv_device_runtime_handle_event(fv_device_runtime_t *runtime,
                                    fv_event_t event);
void fv_device_runtime_execute(fv_device_runtime_t *runtime,
                               fv_command_set_t commands);
void fv_device_runtime_shutdown(fv_device_runtime_t *runtime);

#endif
