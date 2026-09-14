#ifndef FV_USB_DEBUG_DISPLAY_H
#define FV_USB_DEBUG_DISPLAY_H
#include "fuse_vault/display.h"
#if FUSE_VAULT_HEADLESS_DEBUG
extern const fv_display_ops_t fv_usb_debug_display_ops;
void fv_usb_debug_set_boot(bool input, bool flash, bool roots, bool sd,
    bool usb, bool connector, bool services, unsigned recovery);
void fv_usb_debug_task(unsigned state, unsigned buttons);
#endif
#endif
