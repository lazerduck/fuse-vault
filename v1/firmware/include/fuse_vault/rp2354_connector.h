#ifndef FUSE_VAULT_RP2354_CONNECTOR_H
#define FUSE_VAULT_RP2354_CONNECTOR_H

#include "fuse_vault/peripheral_safety.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool pins_configured;
} fv_rp2354_connector_t;

void fv_rp2354_connector_init(fv_rp2354_connector_t *connector);

extern const fv_connector_ops_t fv_rp2354_connector_ops;
#if FUSE_VAULT_BENCH_FIXED_USB_C
/* samples, transitions, last 4-bit pattern, fixed-route flag, 16 histogram bins.
 * Pattern bits: GPIO2, GPIO3, GPIO16, GPIO17. Single-core diagnostics only. */
void fv_rp2354_connector_diagnostic_snapshot(uint32_t out[20]);
#endif

#endif
