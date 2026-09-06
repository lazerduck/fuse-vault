#ifndef FUSE_VAULT_RP2354_SD_SPI_H
#define FUSE_VAULT_RP2354_SD_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_SD_SPI_INIT_HZ 400000u
#define FV_SD_SPI_DATA_HZ 8000000u

typedef struct {
    int sm;
    int tx_dma;
    int rx_dma;
    unsigned offset;
    bool initialized;
    bool failed;
} fv_rp2354_sd_spi_t;

/* One PIO0 state machine and two DMA channels; callers serialize transfers. */
bool fv_rp2354_sd_spi_init(fv_rp2354_sd_spi_t *bus);
void fv_rp2354_sd_spi_deinit(fv_rp2354_sd_spi_t *bus);
bool fv_rp2354_sd_spi_set_speed(fv_rp2354_sd_spi_t *bus, uint32_t hz);
/* NULL TX clocks 0xff; NULL RX discards input. Failure latches until reinit. */
bool fv_rp2354_sd_spi_transfer(fv_rp2354_sd_spi_t *bus,
                              const uint8_t *tx, uint8_t *rx, size_t length);
#endif
