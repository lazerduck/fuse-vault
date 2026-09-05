#ifndef FUSE_VAULT_PERIPHERAL_SAFETY_H
#define FUSE_VAULT_PERIPHERAL_SAFETY_H

#include "fuse_vault/block_device.h"

#include <stdbool.h>

typedef void (*fv_peripheral_fault_fn)(void *context);

typedef enum {
    FV_CONNECTOR_NONE = 0,
    FV_CONNECTOR_USB_A,
    FV_CONNECTOR_USB_C,
    FV_CONNECTOR_CONFLICT,
} fv_connector_state_t;

typedef struct {
    bool (*disable_mux)(void *context);
    bool (*configure_presence_inputs)(void *context);
    bool (*read_usb_a_present)(void *context, bool *present);
    bool (*read_usb_c_present)(void *context, bool *present);
} fv_connector_ops_t;

typedef struct {
    const fv_connector_ops_t *ops;
    void *context;
    fv_peripheral_fault_fn fault;
    void *fault_context;
    fv_connector_state_t state;
    bool initialized;
    bool faulted;
} fv_connector_safety_t;

bool fv_connector_safety_init(fv_connector_safety_t *safety,
                              const fv_connector_ops_t *ops, void *context,
                              fv_peripheral_fault_fn fault,
                              void *fault_context);
bool fv_connector_safety_poll(fv_connector_safety_t *safety);
void fv_connector_safety_fault(fv_connector_safety_t *safety);

typedef struct {
    fv_block_device_t interface;
    fv_block_device_t *raw;
    bool (*card_present)(void *context);
    void *detect_context;
    fv_peripheral_fault_fn fault;
    void *fault_context;
    bool ready;
    bool faulted;
} fv_removable_block_t;

bool fv_removable_block_init(fv_removable_block_t *guard,
                             fv_block_device_t *raw,
                             bool (*card_present)(void *context),
                             void *detect_context,
                             fv_peripheral_fault_fn fault,
                             void *fault_context);
void fv_removable_block_poll(fv_removable_block_t *guard);
void fv_removable_block_reset(fv_removable_block_t *guard);

#endif
