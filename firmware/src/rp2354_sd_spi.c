#include "fuse_vault/rp2354_sd_spi.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "sd_spi.pio.h"

#include <string.h>

static void stop(fv_rp2354_sd_spi_t *bus) {
    pio_sm_set_enabled(pio0, (uint)bus->sm, false);
    dma_channel_abort((uint)bus->tx_dma);
    dma_channel_abort((uint)bus->rx_dma);
    pio_sm_clear_fifos(pio0, (uint)bus->sm);
    pio_sm_set_pins_with_mask(pio0, (uint)bus->sm,
        1u << FUSE_VAULT_SD_CMD_PIN,
        (1u << FUSE_VAULT_SD_CLK_PIN) | (1u << FUSE_VAULT_SD_CMD_PIN));
    bus->failed = true;
}

bool fv_rp2354_sd_spi_set_speed(fv_rp2354_sd_spi_t *bus, uint32_t hz) {
    if (bus == NULL || !bus->initialized || bus->failed || hz == 0u ||
        hz > FV_SD_SPI_DATA_HZ) return false;
    /* Round UP the divider so SCK never exceeds the requested limit. */
    const uint64_t denominator = (uint64_t)hz * 4u;
    const uint64_t divider = ((uint64_t)clock_get_hz(clk_sys) * 256u +
                              denominator - 1u) / denominator;
    if (divider < 256u || divider > 0xffffffu) return false;
    pio_sm_set_enabled(pio0, (uint)bus->sm, false);
    pio_sm_set_clkdiv_int_frac8(pio0, (uint)bus->sm,
        (uint32_t)(divider >> 8u), (uint8_t)divider);
    pio_sm_clkdiv_restart(pio0, (uint)bus->sm);
    pio_sm_set_enabled(pio0, (uint)bus->sm, true);
    return true;
}

bool fv_rp2354_sd_spi_init(fv_rp2354_sd_spi_t *bus) {
    if (bus == NULL) return false;
    memset(bus, 0, sizeof(*bus));
    bus->sm = bus->tx_dma = bus->rx_dma = -1;
    if (!pio_can_add_program(pio0, &fv_sd_spi_program)) return false;
    bus->sm = pio_claim_unused_sm(pio0, false);
    if (bus->sm < 0) return false;
    bus->tx_dma = dma_claim_unused_channel(false);
    bus->rx_dma = dma_claim_unused_channel(false);
    if (bus->tx_dma < 0 || bus->rx_dma < 0) {
        if (bus->tx_dma >= 0) dma_channel_unclaim((uint)bus->tx_dma);
        if (bus->rx_dma >= 0) dma_channel_unclaim((uint)bus->rx_dma);
        pio_sm_unclaim(pio0, (uint)bus->sm);
        return false;
    }
    const int offset = pio_add_program(pio0, &fv_sd_spi_program);
    if (offset < 0) {
        dma_channel_unclaim((uint)bus->tx_dma);
        dma_channel_unclaim((uint)bus->rx_dma);
        pio_sm_unclaim(pio0, (uint)bus->sm);
        return false;
    }
    bus->offset = (unsigned)offset;
    pio_sm_config config = fv_sd_spi_program_get_default_config(bus->offset);
    sm_config_set_out_pins(&config, FUSE_VAULT_SD_CMD_PIN, 1u);
    sm_config_set_in_pins(&config, FUSE_VAULT_SD_DAT0_PIN);
    sm_config_set_sideset_pins(&config, FUSE_VAULT_SD_CLK_PIN);
    sm_config_set_out_shift(&config, false, true, 8u);
    sm_config_set_in_shift(&config, false, true, 8u);
    pio_sm_init(pio0, (uint)bus->sm, bus->offset, &config);
    const uint32_t outputs = (1u << FUSE_VAULT_SD_CLK_PIN) |
                             (1u << FUSE_VAULT_SD_CMD_PIN);
    pio_sm_set_pins_with_mask(pio0, (uint)bus->sm,
                              1u << FUSE_VAULT_SD_CMD_PIN, outputs);
    pio_sm_set_pindirs_with_mask(pio0, (uint)bus->sm, outputs,
                                 outputs | (1u << FUSE_VAULT_SD_DAT0_PIN));
    pio_gpio_init(pio0, FUSE_VAULT_SD_CLK_PIN);
    pio_gpio_init(pio0, FUSE_VAULT_SD_CMD_PIN);
    pio_gpio_init(pio0, FUSE_VAULT_SD_DAT0_PIN);
    /* Keep the input synchronizer: 8 MHz leaves ample sample timing margin. */
    bus->initialized = true;
    if (fv_rp2354_sd_spi_set_speed(bus, FV_SD_SPI_INIT_HZ)) return true;
    fv_rp2354_sd_spi_deinit(bus);
    return false;
}

bool fv_rp2354_sd_spi_transfer(fv_rp2354_sd_spi_t *bus,
                              const uint8_t *tx, uint8_t *rx, size_t length) {
    if (bus == NULL || !bus->initialized || bus->failed ||
        length == 0u || length > 512u) return false;
    const uint sm = (uint)bus->sm;
    uint8_t fill = 0xffu, discard;
    const absolute_time_t deadline = make_timeout_time_us(100000u);
    dma_channel_config receive = dma_channel_get_default_config((uint)bus->rx_dma);
    channel_config_set_transfer_data_size(&receive, DMA_SIZE_8);
    channel_config_set_read_increment(&receive, false);
    channel_config_set_write_increment(&receive, rx != NULL);
    channel_config_set_dreq(&receive, pio_get_dreq(pio0, sm, false));
    dma_channel_configure((uint)bus->rx_dma, &receive, rx ? rx : &discard,
                           &pio0->rxf[sm], (uint32_t)length, false);
    dma_channel_config transmit = dma_channel_get_default_config((uint)bus->tx_dma);
    channel_config_set_transfer_data_size(&transmit, DMA_SIZE_8);
    channel_config_set_read_increment(&transmit, tx != NULL);
    channel_config_set_write_increment(&transmit, false);
    channel_config_set_dreq(&transmit, pio_get_dreq(pio0, sm, true));
    dma_channel_configure((uint)bus->tx_dma, &transmit, &pio0->txf[sm],
                           tx ? tx : &fill, (uint32_t)length, false);
    dma_start_channel_mask((1u << (uint)bus->rx_dma) | (1u << (uint)bus->tx_dma));
    while (dma_channel_is_busy((uint)bus->rx_dma) ||
           dma_channel_is_busy((uint)bus->tx_dma)) {
        if (time_reached(deadline)) {
            stop(bus);
            if (rx != NULL) memset(rx, 0, length);
            return false;
        }
        tight_loop_contents();
    }
    const uint32_t errors = DMA_CH0_CTRL_TRIG_READ_ERROR_BITS |
                            DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    if (((dma_hw->ch[(uint)bus->rx_dma].ctrl_trig |
          dma_hw->ch[(uint)bus->tx_dma].ctrl_trig) & errors) != 0u) {
        stop(bus);
        if (rx != NULL) memset(rx, 0, length);
        return false;
    }
    /* RX completion follows the last rising edge. Wait for the OUT stall to
     * prove the last falling edge occurred before the caller changes CS. */
    const uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio0->fdebug = stalled;
    while ((pio0->fdebug & stalled) == 0u) {
        if (time_reached(deadline)) {
            stop(bus);
            if (rx != NULL) memset(rx, 0, length);
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

void fv_rp2354_sd_spi_deinit(fv_rp2354_sd_spi_t *bus) {
    if (bus == NULL || !bus->initialized) return;
    stop(bus);
    dma_channel_cleanup((uint)bus->tx_dma);
    dma_channel_cleanup((uint)bus->rx_dma);
    dma_channel_unclaim((uint)bus->tx_dma);
    dma_channel_unclaim((uint)bus->rx_dma);
    pio_remove_program(pio0, &fv_sd_spi_program, bus->offset);
    pio_sm_unclaim(pio0, (uint)bus->sm);
    gpio_put(FUSE_VAULT_SD_CLK_PIN, false);
    gpio_put(FUSE_VAULT_SD_CMD_PIN, true);
    gpio_set_function(FUSE_VAULT_SD_CLK_PIN, GPIO_FUNC_SIO);
    gpio_set_function(FUSE_VAULT_SD_CMD_PIN, GPIO_FUNC_SIO);
    gpio_set_function(FUSE_VAULT_SD_DAT0_PIN, GPIO_FUNC_SIO);
    bus->initialized = false;
}
