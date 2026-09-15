/* Native four-bit bench transport; preserves the raw block-device contract. */
#include "fuse_vault/rp2354_sd.h"
#include "fuse_vault/storage_profile.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hw_config.h"
#include "SDIO/rp2040_sdio.h"
#include <string.h>

_Static_assert(FUSE_VAULT_SD_DAT1_PIN == FUSE_VAULT_SD_DAT0_PIN + 1 &&
    FUSE_VAULT_SD_DAT2_PIN == FUSE_VAULT_SD_DAT0_PIN + 2 &&
    FUSE_VAULT_SD_DAT3_PIN == FUSE_VAULT_SD_DAT0_PIN + 3 &&
    FUSE_VAULT_SD_CLK_PIN == (FUSE_VAULT_SD_DAT0_PIN + 30) % 32,
    "Native PIO SD pin layout mismatch");
static sd_sdio_if_t native_if = {
    .CMD_gpio = FUSE_VAULT_SD_CMD_PIN, .D0_gpio = FUSE_VAULT_SD_DAT0_PIN,
    .SDIO_PIO = pio1, .DMA_IRQ_num = DMA_IRQ_1, .baud_rate = 25000000u,
};
static sd_card_t card = {.type = SD_IF_SDIO, .sdio_if_p = &native_if};
size_t sd_get_num(void) { return 1u; }
sd_card_t *sd_get_by_num(size_t number) { return number == 0u ? &card : NULL; }

static bool detected(void) {
#if FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
    return gpio_get(FUSE_VAULT_SD_CARD_DETECT_PIN) == FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL;
#else
    return true;
#endif
}
/* Stop outstanding DMA before caller buffers can leave scope, including errors.
 * Resource claims remain owned by this singleton and are reused on reinit. */
static void halt(void) {
    if (native_if.state.resources_claimed) {
        dma_channel_set_irq1_enabled((uint)native_if.state.SDIO_DMA_CHB, false);
        pio_sm_set_enabled(pio1, (uint)native_if.state.SDIO_DATA_SM, false);
        pio_sm_set_enabled(pio1, (uint)native_if.state.SDIO_CMD_SM, false);
        dma_channel_abort((uint)native_if.state.SDIO_DMA_CH);
        dma_channel_abort((uint)native_if.state.SDIO_DMA_CHB);
        dma_hw->ints1 = 1u << (uint)native_if.state.SDIO_DMA_CHB;
    }
    native_if.state.ongoing_wr_mlt_blk = false;
    card.state.m_Status |= STA_NOINIT;
}
static void fault(fv_rp2354_sd_t *sd) {
    halt();
    if (!detected()) sd->removal_pending = true;
    else sd->failure_pending = true;
    sd->initialized = false;
    sd->blocks = 0;
}
static bool initialize(fv_rp2354_sd_t *sd) {
    sd->initialized = false; sd->blocks = 0;
    if (!detected()) return false;
    if (!sd_init_driver()) return false;
    card.state.m_Status |= STA_NOINIT;
    if (card.init(&card) != 0u || card.state.sectors == 0u ||
        !(native_if.state.ocr & (1u << 30))) {
        /* Only sector-addressed SDHC/SDXC cards in this initial backend. */
        halt(); return false;
    }
    sd->blocks = card.state.sectors;
    sd->high_capacity = true; sd->initialized = true;
    return true;
}
static fv_block_result_t validate(fv_block_device_t *device, uint64_t first,
                                  uint32_t count, const void *buffer) {
    if (!device || !device->context || !buffer || !count) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    fv_rp2354_sd_t *sd = device->context;
    if (!sd->initialized) return FV_BLOCK_ERROR_NOT_READY;
    if (!detected()) {fault(sd);return FV_BLOCK_ERROR_NOT_READY;}
    if (first >= sd->blocks || count > sd->blocks - first) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    return FV_BLOCK_OK;
}
/* Complete the multi-block command, then require READY_FOR_DATA and TRAN state.
 * Command status is checked even though data CRC tokens were already checked. */
static bool finish(void) {
    if (card.sync(&card) != SD_BLOCK_DEVICE_ERROR_NONE) return false;
    absolute_time_t deadline = make_timeout_time_us(1000000u);
    do {
        uint32_t status = 0;
        if (rp2040_sdio_command_R1(&card, 13u, native_if.state.rca, &status) != SDIO_OK)
            return false;
        if (status & UINT32_C(0xfdffe008)) return false;
        if ((status & (1u << 8)) && ((status >> 9) & 15u) == 4u) return true;
    } while (!time_reached(deadline));
    return false;
}
static fv_block_result_t read_blocks(fv_block_device_t *device, uint64_t first,
                                    uint32_t count, uint8_t *output) {
    fv_block_result_t r = validate(device, first, count, output);
    if (r != FV_BLOCK_OK) return r;
    fv_rp2354_sd_t *sd = device->context;
    uint64_t start = fv_storage_profile_begin();
    uint32_t done = 0;
    while (done < count) {
        uint32_t n = count-done > SDIO_MAX_BLOCKS ? SDIO_MAX_BLOCKS : count-done;
        if (card.read_blocks(&card, output+(size_t)done*512u, (uint32_t)first+done, n) != SD_BLOCK_DEVICE_ERROR_NONE) {
            fault(sd);memset(output,0,(size_t)count*512u);r=FV_BLOCK_ERROR_IO;break;
        }
        done += n;
    }
    fv_storage_profile_end(FV_PERF_SD_READ,start);
    return r;
}
static fv_block_result_t write_blocks(fv_block_device_t *device, uint64_t first,
                                     uint32_t count, const uint8_t *input) {
    fv_block_result_t r = validate(device, first, count, input);
    if (r != FV_BLOCK_OK) return r;
    fv_rp2354_sd_t *sd = device->context;
    uint64_t start = fv_storage_profile_begin();
    uint32_t done = 0;
    while (done < count) {
        uint32_t n = count-done > SDIO_MAX_BLOCKS ? SDIO_MAX_BLOCKS : count-done;
        if (card.write_blocks(&card, input+(size_t)done*512u, (uint32_t)first+done, n) != SD_BLOCK_DEVICE_ERROR_NONE || !finish()) {
            fault(sd);r=FV_BLOCK_ERROR_IO;break;
        }
        done += n;
    }
    fv_storage_profile_end(FV_PERF_SD_WRITE,start);
    return r;
}
static fv_block_result_t sync_blocks(fv_block_device_t *device) {
    if (!device || !device->context) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    fv_rp2354_sd_t *sd=device->context;
    if (!sd->initialized) return FV_BLOCK_ERROR_NOT_READY;
    uint64_t start=fv_storage_profile_begin();
    bool ok=detected() && finish();
    if (!ok) fault(sd);
    fv_storage_profile_end(FV_PERF_SD_SYNC,start);
    return ok ? FV_BLOCK_OK : FV_BLOCK_ERROR_IO;
}
static uint64_t block_count(const fv_block_device_t *device) {
    const fv_rp2354_sd_t *sd=device?device->context:NULL;
    return sd && sd->initialized && detected() ? sd->blocks : 0;
}
static bool is_present(const fv_block_device_t *device) {return block_count(device)!=0;}
static const fv_block_device_ops_t ops={read_blocks,write_blocks,sync_blocks,block_count,is_present};
bool fv_rp2354_sd_init(fv_rp2354_sd_t *sd) {
    if (!sd) return false;
    memset(sd,0,sizeof(*sd));sd->interface.ops=&ops;sd->interface.context=sd;
#if FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
    gpio_init(FUSE_VAULT_SD_CARD_DETECT_PIN);gpio_set_dir(FUSE_VAULT_SD_CARD_DETECT_PIN,GPIO_IN);
    gpio_disable_pulls(FUSE_VAULT_SD_CARD_DETECT_PIN);sd->detect_configured=true;
#endif
    sd->physical_present=detected();
    if(sd->physical_present && !initialize(sd))sd->failure_pending=true;
    return true;
}
bool fv_rp2354_sd_reinitialize(fv_rp2354_sd_t *sd) {
    if(!sd || sd->interface.ops!=&ops)return false;
    halt();sd->physical_present=detected();
    if(!initialize(sd)){sd->failure_pending=sd->physical_present;return false;}
    sd->insertion_pending=true;return true;
}
void fv_rp2354_sd_poll(fv_rp2354_sd_t *sd) {
    if(!sd)return;
    bool present=detected();
    if(sd->initialized && !present)fault(sd);
    if(!present)sd->physical_present=false;
    else if(!sd->initialized && !sd->physical_present){
        sd->physical_present=true;
        if(initialize(sd))sd->insertion_pending=true;else sd->failure_pending=true;
    }
}
#define EVENT(name,field) bool name(fv_rp2354_sd_t *sd){if(!sd)return false;bool v=sd->field;sd->field=false;return v;}
EVENT(fv_rp2354_sd_take_insertion_event,insertion_pending)
EVENT(fv_rp2354_sd_take_removal_event,removal_pending)
EVENT(fv_rp2354_sd_take_failure_event,failure_pending)
void fv_rp2354_sd_deinit(fv_rp2354_sd_t *sd) {
    if(!sd)return;
    halt();if(card.deinit)card.deinit(&card);
    sd->initialized=false;sd->blocks=0;sd->physical_present=false;
    sd->insertion_pending=sd->removal_pending=sd->failure_pending=false;
}
