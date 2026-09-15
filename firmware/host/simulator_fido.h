#ifndef FV_SIMULATOR_FIDO_H
#define FV_SIMULATOR_FIDO_H

#include "fuse_vault/device_runtime.h"
#include "fuse_vault/fido_store.h"
#include "fuse_vault/fido_verification.h"

/* Real authenticator, with host-only UI callbacks. No OS USB device is created. */
typedef struct {
    fv_fido_store_t store;
    fv_fido_verification_t verification;
    fv_device_runtime_t *runtime;
    uint32_t (*millis)(void *);
    int (*presence)(void *, bool reset);
    void *context;
    bool attached, failed;
    uint8_t command;
} fv_simulator_fido_t;

void fv_simulator_fido_init(fv_simulator_fido_t *fido,
    fv_device_runtime_t *runtime, uint32_t (*millis)(void *),
    int (*presence)(void *, bool), void *context);
void fv_simulator_fido_close(fv_simulator_fido_t *fido);
void fv_simulator_fido_session(fv_simulator_fido_t *fido, bool was_unlocked);
bool fv_simulator_fido_attach(fv_simulator_fido_t *fido,
    const fv_volume_master_key_t *vmk, const fv_media_layout_t *layout,
    const fv_encryption_stack_descriptor_t *stack);
bool fv_simulator_fido_manage(fv_simulator_fido_t *fido,
    fv_passkey_action_t action, uint16_t *index, uint16_t *count, fv_passkey_t *entry);
size_t fv_simulator_fido_command(fv_simulator_fido_t *fido,
    const uint8_t *request, size_t size, uint8_t *response, size_t capacity);
#endif
