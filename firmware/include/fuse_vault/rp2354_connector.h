#ifndef FUSE_VAULT_RP2354_CONNECTOR_H
#define FUSE_VAULT_RP2354_CONNECTOR_H

#include "fuse_vault/peripheral_safety.h"

#include <stdbool.h>

typedef struct {
    bool pins_configured;
} fv_rp2354_connector_t;

void fv_rp2354_connector_init(fv_rp2354_connector_t *connector);

extern const fv_connector_ops_t fv_rp2354_connector_ops;

#endif
