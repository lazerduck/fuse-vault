#include "fuse_vault/peripheral_safety.h"

#include <stddef.h>
#include <string.h>

static void connector_trip(fv_connector_safety_t *safety) {
    if (safety == NULL) return;
    if (safety->ops != NULL && safety->ops->disable_mux != NULL)
        (void)safety->ops->disable_mux(safety->context);
    safety->faulted = true;
    safety->initialized = false;
    safety->state = FV_CONNECTOR_NONE;
    if (safety->fault != NULL) safety->fault(safety->fault_context);
}

bool fv_connector_safety_init(fv_connector_safety_t *safety,
                              const fv_connector_ops_t *ops, void *context,
                              fv_peripheral_fault_fn fault,
                              void *fault_context) {
    if (safety == NULL || ops == NULL || ops->disable_mux == NULL ||
        ops->configure_presence_inputs == NULL ||
        ops->read_usb_a_present == NULL ||
        ops->read_usb_c_present == NULL) return false;
    memset(safety, 0, sizeof(*safety));
    safety->ops = ops;
    safety->context = context;
    safety->fault = fault;
    safety->fault_context = fault_context;
    /* This must be the first hardware operation. Selection and enable are
       deliberately absent until the board mux truth table is reviewed. */
    if (!ops->disable_mux(context) || !ops->configure_presence_inputs(context)) {
        connector_trip(safety);
        return false;
    }
    safety->initialized = true;
    return fv_connector_safety_poll(safety);
}

bool fv_connector_safety_poll(fv_connector_safety_t *safety) {
    if (safety == NULL || !safety->initialized || safety->faulted) return false;
    bool usb_a = false;
    bool usb_c = false;
    if (!safety->ops->read_usb_a_present(safety->context, &usb_a) ||
        !safety->ops->read_usb_c_present(safety->context, &usb_c)) {
        connector_trip(safety);
        return false;
    }
    safety->state = usb_a && usb_c ? FV_CONNECTOR_CONFLICT
                  : usb_a ? FV_CONNECTOR_USB_A
                  : usb_c ? FV_CONNECTOR_USB_C : FV_CONNECTOR_NONE;
    if (safety->state == FV_CONNECTOR_CONFLICT) {
        connector_trip(safety);
        return false;
    }
    return true;
}

void fv_connector_safety_fault(fv_connector_safety_t *safety) {
    connector_trip(safety);
}

static void block_trip(fv_removable_block_t *guard) {
    guard->ready = false;
    if (!guard->faulted) {
        guard->faulted = true;
        if (guard->fault != NULL) guard->fault(guard->fault_context);
    }
}

static bool block_present(const fv_block_device_t *device) {
    const fv_removable_block_t *guard = device->context;
    return guard->ready && !guard->faulted &&
           guard->card_present(guard->detect_context) &&
           guard->raw->ops->is_present(guard->raw);
}

static fv_block_result_t preflight(fv_removable_block_t *guard) {
    if (!block_present(&guard->interface)) {
        block_trip(guard);
        return FV_BLOCK_ERROR_NOT_READY;
    }
    return FV_BLOCK_OK;
}

static fv_block_result_t guarded_read(fv_block_device_t *device, uint64_t first,
                                      uint32_t count, uint8_t *output) {
    fv_removable_block_t *guard = device->context;
    fv_block_result_t result = preflight(guard);
    if (result == FV_BLOCK_OK)
        result = guard->raw->ops->read(guard->raw, first, count, output);
    if (result == FV_BLOCK_ERROR_IO || result == FV_BLOCK_ERROR_NOT_READY)
        block_trip(guard);
    return result;
}

static fv_block_result_t guarded_write(fv_block_device_t *device, uint64_t first,
                                       uint32_t count, const uint8_t *input) {
    fv_removable_block_t *guard = device->context;
    fv_block_result_t result = preflight(guard);
    if (result == FV_BLOCK_OK)
        result = guard->raw->ops->write(guard->raw, first, count, input);
    if (result == FV_BLOCK_ERROR_IO || result == FV_BLOCK_ERROR_NOT_READY)
        block_trip(guard);
    return result;
}

static fv_block_result_t guarded_sync(fv_block_device_t *device) {
    fv_removable_block_t *guard = device->context;
    fv_block_result_t result = preflight(guard);
    if (result == FV_BLOCK_OK) result = guard->raw->ops->sync(guard->raw);
    if (result == FV_BLOCK_ERROR_IO || result == FV_BLOCK_ERROR_NOT_READY)
        block_trip(guard);
    return result;
}

static uint64_t guarded_count(const fv_block_device_t *device) {
    const fv_removable_block_t *guard = device->context;
    return block_present(device) ? guard->raw->ops->block_count(guard->raw) : 0u;
}

static const fv_block_device_ops_t guarded_ops = {
    .read = guarded_read, .write = guarded_write, .sync = guarded_sync,
    .block_count = guarded_count, .is_present = block_present,
};

bool fv_removable_block_init(fv_removable_block_t *guard,
                             fv_block_device_t *raw,
                             bool (*card_present)(void *context),
                             void *detect_context,
                             fv_peripheral_fault_fn fault,
                             void *fault_context) {
    if (guard == NULL || raw == NULL || raw->ops == NULL ||
        raw->ops->read == NULL || raw->ops->write == NULL ||
        raw->ops->sync == NULL || raw->ops->block_count == NULL ||
        raw->ops->is_present == NULL || card_present == NULL) return false;
    memset(guard, 0, sizeof(*guard));
    guard->interface.ops = &guarded_ops;
    guard->interface.context = guard;
    guard->raw = raw;
    guard->card_present = card_present;
    guard->detect_context = detect_context;
    guard->fault = fault;
    guard->fault_context = fault_context;
    guard->ready = card_present(detect_context) && raw->ops->is_present(raw);
    return guard->ready;
}

void fv_removable_block_poll(fv_removable_block_t *guard) {
    if (guard != NULL && guard->ready && !block_present(&guard->interface))
        block_trip(guard);
}

void fv_removable_block_reset(fv_removable_block_t *guard) {
    if (guard == NULL) return;
    guard->ready = false;
    guard->faulted = false;
}
