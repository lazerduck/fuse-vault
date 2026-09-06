#ifndef FUSE_VAULT_RP2354_DISPLAY_H
#define FUSE_VAULT_RP2354_DISPLAY_H

#include "fuse_vault/display.h"

typedef struct {
    bool initialized;
} fv_rp2354_display_t;

extern const fv_display_ops_t fv_rp2354_display_ops;

#endif
