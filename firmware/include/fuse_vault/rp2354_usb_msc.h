#ifndef FUSE_VAULT_RP2354_USB_MSC_H
#define FUSE_VAULT_RP2354_USB_MSC_H

#include "fuse_vault/block_device.h"

#include <stdbool.h>

/* Initialization retains no block device and does not enumerate. The USB data
 * interface is connected only by attach after successful local unlock. */
bool fv_rp2354_usb_msc_init(void);
bool fv_rp2354_usb_msc_attach(fv_block_device_t *plaintext_blocks);
bool fv_rp2354_usb_msc_detach(void);
void fv_rp2354_usb_msc_task(void);
bool fv_rp2354_usb_msc_take_eject_request(void);
bool fv_rp2354_usb_msc_take_storage_failure(void);

#endif
