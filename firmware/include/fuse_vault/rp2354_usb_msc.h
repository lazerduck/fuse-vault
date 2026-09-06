#ifndef FUSE_VAULT_RP2354_USB_MSC_H
#define FUSE_VAULT_RP2354_USB_MSC_H

#include "fuse_vault/block_device.h"
#include "fuse_vault/fido_probe.h"

#include <stdbool.h>

/* Initialization retains no block device and does not enumerate. The USB data
 * interface is connected only by attach after successful local unlock. */
bool fv_rp2354_usb_msc_init(void);
bool fv_rp2354_usb_msc_attach(fv_block_device_t *plaintext_blocks);
/* Development FIDO mode; shares the sole USB owner. Device code binds the engine. */
bool fv_rp2354_usb_fido_attach(void);
bool fv_rp2354_usb_fido_attach_engine(fv_fido_dispatch_fn dispatch, void *context);
/* Pump USB during a synchronous engine operation. False means cancel/failure. */
bool fv_rp2354_usb_fido_cancelled(void);
bool fv_rp2354_usb_fido_poll(uint32_t now_ms, uint8_t keepalive_status);
bool fv_rp2354_usb_is_fido(void);
void fv_rp2354_usb_task_at(uint32_t now_ms);
bool fv_rp2354_usb_msc_detach(void);
void fv_rp2354_usb_msc_task(void);
bool fv_rp2354_usb_msc_take_eject_request(void);
bool fv_rp2354_usb_msc_take_storage_failure(void);

#endif
