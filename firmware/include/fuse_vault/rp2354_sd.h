#ifndef FUSE_VAULT_RP2354_SD_H
#define FUSE_VAULT_RP2354_SD_H

#include "fuse_vault/block_device.h"

#include <stdbool.h>
#include <stdint.h>

/* Baseline SD memory-card backend using SPI mode on the board's SD pins.
 * This intentionally favors a small, auditable bring-up path over throughput;
 * the block-device boundary permits a later four-bit PIO SDIO replacement. */
typedef struct {
    fv_block_device_t interface;
    uint64_t blocks;
    bool initialized;
    bool high_capacity;
    bool detect_configured;
    bool physical_present;
    bool insertion_pending;
    bool removal_pending;
    bool failure_pending;
} fv_rp2354_sd_t;

/* Initializes the driver and pins. A missing card is a valid initialized
 * driver state; is_present remains false until a card is detected and its CSD
 * has been validated. */
bool fv_rp2354_sd_init(fv_rp2354_sd_t *sd);
bool fv_rp2354_sd_reinitialize(fv_rp2354_sd_t *sd);
void fv_rp2354_sd_poll(fv_rp2354_sd_t *sd);
bool fv_rp2354_sd_take_insertion_event(fv_rp2354_sd_t *sd);
bool fv_rp2354_sd_take_removal_event(fv_rp2354_sd_t *sd);
bool fv_rp2354_sd_take_failure_event(fv_rp2354_sd_t *sd);
void fv_rp2354_sd_deinit(fv_rp2354_sd_t *sd);

#endif
