#include "fuse_vault/rp2354_sd.h"

#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SD_CMD0 0u
#define SD_CMD8 8u
#define SD_CMD9 9u
#define SD_CMD13 13u
#define SD_CMD16 16u
#define SD_CMD17 17u
#define SD_CMD24 24u
#define SD_CMD41 41u
#define SD_CMD55 55u
#define SD_CMD58 58u
#define SD_CMD59 59u

#define SD_R1_IDLE 0x01u
#define SD_R1_ILLEGAL_COMMAND 0x04u
#define SD_DATA_TOKEN 0xfeu
#define SD_WRITE_ACCEPTED 0x05u
#define SD_COMMAND_TIMEOUT_US UINT64_C(250000)
#define SD_INIT_TIMEOUT_US UINT64_C(2000000)
#define SD_WRITE_TIMEOUT_US UINT64_C(1000000)

#if FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
#if !defined(FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL)
#error "Confirmed SD card-detect builds must define the active level"
#endif
_Static_assert(FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL == 0 ||
                   FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL == 1,
               "SD card-detect active level must be 0 or 1");
#endif

static bool card_detected(const fv_rp2354_sd_t *sd) {
#if !FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
    (void)sd;
    return true;
#else
    return sd != NULL && sd->detect_configured &&
           gpio_get(FUSE_VAULT_SD_CARD_DETECT_PIN) ==
               FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL;
#endif
}

static void mark_not_ready(fv_rp2354_sd_t *sd, bool operation_failed) {
    if (sd == NULL) return;
    if (sd->initialized && !card_detected(sd)) sd->removal_pending = true;
    else if (sd->initialized && operation_failed) sd->failure_pending = true;
    sd->initialized = false;
    sd->blocks = 0u;
}

static uint8_t spi_transfer(uint8_t output) {
    uint8_t input = 0u;
    for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1u) {
        gpio_put(FUSE_VAULT_SD_CMD_PIN, (output & mask) != 0u);
        gpio_put(FUSE_VAULT_SD_CLK_PIN, true);
        input = (uint8_t)((input << 1u) |
                          (gpio_get(FUSE_VAULT_SD_DAT0_PIN) ? 1u : 0u));
        gpio_put(FUSE_VAULT_SD_CLK_PIN, false);
    }
    return input;
}

static void deselect_card(void) {
    gpio_put(FUSE_VAULT_SD_DAT3_PIN, true);
    (void)spi_transfer(0xffu);
}

static bool wait_byte(uint8_t expected, uint64_t timeout_us,
                      uint8_t *received) {
    const absolute_time_t deadline = make_timeout_time_us(timeout_us);
    uint8_t value;
    do {
        value = spi_transfer(0xffu);
        if (value == expected) {
            if (received != NULL) *received = value;
            return true;
        }
    } while (!time_reached(deadline));
    if (received != NULL) *received = value;
    return false;
}

static bool select_card(void) {
    gpio_put(FUSE_VAULT_SD_DAT3_PIN, false);
    (void)spi_transfer(0xffu);
    return wait_byte(0xffu, SD_COMMAND_TIMEOUT_US, NULL);
}

static uint8_t crc7(const uint8_t *data, size_t length) {
    uint8_t crc = 0u;
    for (size_t index = 0u; index < length; ++index) {
        uint8_t value = data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (uint8_t)(crc << 1u);
            if (((value ^ crc) & 0x80u) != 0u) crc ^= 0x09u;
            value = (uint8_t)(value << 1u);
        }
    }
    return (uint8_t)((crc << 1u) | 1u);
}

static uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0u;
    for (size_t index = 0u; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8u;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)((crc << 1u) ^ 0x1021u)
                : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool crc_implementation_valid(void) {
    static const uint8_t cmd0[5] = {0x40u, 0u, 0u, 0u, 0u};
    static const uint8_t cmd8[5] = {0x48u, 0u, 0u, 0x01u, 0xaau};
    return crc7(cmd0, sizeof(cmd0)) == 0x95u &&
           crc7(cmd8, sizeof(cmd8)) == 0x87u;
}

static uint8_t send_command(uint8_t command, uint32_t argument,
                            uint8_t *extra, size_t extra_length) {
    uint8_t packet[6] = {
        (uint8_t)(0x40u | command),
        (uint8_t)(argument >> 24u),
        (uint8_t)(argument >> 16u),
        (uint8_t)(argument >> 8u),
        (uint8_t)argument,
        0u,
    };
    packet[5] = crc7(packet, 5u);
    if (!select_card()) {
        deselect_card();
        return 0xffu;
    }
    for (size_t index = 0u; index < sizeof(packet); ++index) {
        (void)spi_transfer(packet[index]);
    }
    uint8_t response = 0xffu;
    for (unsigned attempt = 0u; attempt < 16u; ++attempt) {
        response = spi_transfer(0xffu);
        if ((response & 0x80u) == 0u) break;
    }
    if ((response & 0x80u) == 0u) {
        for (size_t index = 0u; index < extra_length; ++index) {
            extra[index] = spi_transfer(0xffu);
        }
    }
    return response;
}

static bool command_only(uint8_t command, uint32_t argument,
                         uint8_t expected) {
    const uint8_t response = send_command(command, argument, NULL, 0u);
    deselect_card();
    return response == expected;
}

static bool read_register(uint8_t command, uint8_t *output, size_t length) {
    if (send_command(command, 0u, NULL, 0u) != 0u ||
        !wait_byte(SD_DATA_TOKEN, SD_COMMAND_TIMEOUT_US, NULL)) {
        deselect_card();
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        output[index] = spi_transfer(0xffu);
    }
    const uint16_t stored_crc = (uint16_t)spi_transfer(0xffu) << 8u |
                                spi_transfer(0xffu);
    deselect_card();
    return stored_crc == crc16(output, length);
}

static bool parse_capacity(const uint8_t csd[16], uint64_t *blocks) {
    const uint8_t structure = (uint8_t)(csd[0] >> 6u);
    if (structure == 1u) {
        const uint32_t c_size = ((uint32_t)(csd[7] & 0x3fu) << 16u) |
                                ((uint32_t)csd[8] << 8u) | csd[9];
        *blocks = ((uint64_t)c_size + 1u) * 1024u;
        return *blocks != 0u;
    }
    if (structure == 0u) {
        const uint8_t read_length = (uint8_t)(csd[5] & 0x0fu);
        const uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10u) |
                                ((uint32_t)csd[7] << 2u) |
                                ((uint32_t)csd[8] >> 6u);
        const uint8_t multiplier = (uint8_t)(
            ((csd[9] & 0x03u) << 1u) | (csd[10] >> 7u));
        if (read_length > 31u || multiplier > 7u) return false;
        const uint64_t bytes = ((uint64_t)c_size + 1u) <<
            (read_length + multiplier + 2u);
        *blocks = bytes / FV_BLOCK_SIZE;
        return *blocks != 0u;
    }
    return false;
}

static bool initialize_card(fv_rp2354_sd_t *sd) {
    sd->initialized = false;
    sd->high_capacity = false;
    sd->blocks = 0u;
    sd->removal_pending = false;
    if (!card_detected(sd)) return false;
    if (!crc_implementation_valid()) return false;
    deselect_card();
    for (unsigned index = 0u; index < 10u; ++index) {
        (void)spi_transfer(0xffu);
    }
    if (!command_only(SD_CMD0, 0u, SD_R1_IDLE)) return false;

    uint8_t r7[4] = {0};
    const uint8_t cmd8 = send_command(SD_CMD8, 0x000001aau, r7, sizeof(r7));
    deselect_card();
    const bool version_two = cmd8 == SD_R1_IDLE;
    if (version_two && (r7[2] != 0x01u || r7[3] != 0xaau)) return false;
    if (!version_two && (cmd8 & SD_R1_ILLEGAL_COMMAND) == 0u) return false;

    const absolute_time_t deadline = make_timeout_time_us(SD_INIT_TIMEOUT_US);
    bool ready = false;
    do {
        if (!command_only(SD_CMD55, 0u, SD_R1_IDLE)) break;
        const uint8_t response = send_command(
            SD_CMD41, version_two ? UINT32_C(0x40000000) : 0u, NULL, 0u);
        deselect_card();
        if (response == 0u) {
            ready = true;
            break;
        }
        if (response != SD_R1_IDLE) break;
    } while (!time_reached(deadline));
    if (!ready) return false;

    uint8_t ocr[4] = {0};
    if (send_command(SD_CMD58, 0u, ocr, sizeof(ocr)) != 0u) {
        deselect_card();
        return false;
    }
    deselect_card();
    sd->high_capacity = (ocr[0] & 0x40u) != 0u;
    if (!sd->high_capacity && !command_only(SD_CMD16, FV_BLOCK_SIZE, 0u)) {
        return false;
    }

    /* CRC checking is mandatory on our side. Ask the card to validate command
     * and write CRC too; older cards may legally reject CMD59 in SPI mode. */
    const uint8_t crc_response = send_command(SD_CMD59, 1u, NULL, 0u);
    deselect_card();
    if (crc_response != 0u &&
        (crc_response & SD_R1_ILLEGAL_COMMAND) == 0u) {
        return false;
    }

    uint8_t csd[16];
    if (!read_register(SD_CMD9, csd, sizeof(csd)) ||
        !parse_capacity(csd, &sd->blocks)) {
        memset(csd, 0, sizeof(csd));
        return false;
    }
    memset(csd, 0, sizeof(csd));
    sd->initialized = true;
    return true;
}

static bool address_for_block(const fv_rp2354_sd_t *sd, uint64_t block,
                              uint32_t *address) {
    if (block >= sd->blocks) return false;
    if (sd->high_capacity) {
        if (block > UINT32_MAX) return false;
        *address = (uint32_t)block;
        return true;
    }
    if (block > UINT32_MAX / FV_BLOCK_SIZE) return false;
    *address = (uint32_t)block * FV_BLOCK_SIZE;
    return true;
}

static fv_block_result_t read_one(fv_rp2354_sd_t *sd, uint64_t block,
                                  uint8_t output[FV_BLOCK_SIZE]) {
    uint32_t address;
    if (!address_for_block(sd, block, &address)) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    if (send_command(SD_CMD17, address, NULL, 0u) != 0u ||
        !wait_byte(SD_DATA_TOKEN, SD_COMMAND_TIMEOUT_US, NULL)) {
        deselect_card();
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_IO;
    }
    for (size_t index = 0u; index < FV_BLOCK_SIZE; ++index) {
        output[index] = spi_transfer(0xffu);
    }
    const uint16_t stored_crc = (uint16_t)spi_transfer(0xffu) << 8u |
                                spi_transfer(0xffu);
    deselect_card();
    if (stored_crc != crc16(output, FV_BLOCK_SIZE)) {
        memset(output, 0, FV_BLOCK_SIZE);
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_INTEGRITY;
    }
    return FV_BLOCK_OK;
}

static fv_block_result_t write_one(fv_rp2354_sd_t *sd, uint64_t block,
                                   const uint8_t input[FV_BLOCK_SIZE]) {
    uint32_t address;
    if (!address_for_block(sd, block, &address)) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    if (send_command(SD_CMD24, address, NULL, 0u) != 0u) {
        deselect_card();
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_IO;
    }
    (void)spi_transfer(0xffu);
    (void)spi_transfer(SD_DATA_TOKEN);
    for (size_t index = 0u; index < FV_BLOCK_SIZE; ++index) {
        (void)spi_transfer(input[index]);
    }
    const uint16_t data_crc = crc16(input, FV_BLOCK_SIZE);
    (void)spi_transfer((uint8_t)(data_crc >> 8u));
    (void)spi_transfer((uint8_t)data_crc);
    const uint8_t response = (uint8_t)(spi_transfer(0xffu) & 0x1fu);
    const bool completed = response == SD_WRITE_ACCEPTED &&
        wait_byte(0xffu, SD_WRITE_TIMEOUT_US, NULL);
    deselect_card();
    if (!completed) {
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_IO;
    }
    uint8_t status[1] = {0xffu};
    const uint8_t r1 = send_command(SD_CMD13, 0u, status, sizeof(status));
    deselect_card();
    if (r1 != 0u || status[0] != 0u) {
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_IO;
    }
    return FV_BLOCK_OK;
}

static fv_block_result_t read_blocks(fv_block_device_t *device,
                                     uint64_t first_block,
                                     uint32_t block_count, uint8_t *output) {
    fv_rp2354_sd_t *sd = device != NULL ? device->context : NULL;
    if (sd == NULL || output == NULL || block_count == 0u) {
        return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    }
    if (!sd->initialized) return FV_BLOCK_ERROR_NOT_READY;
    if (!card_detected(sd)) {
        mark_not_ready(sd, false);
        return FV_BLOCK_ERROR_NOT_READY;
    }
    if (first_block >= sd->blocks ||
        block_count > sd->blocks - first_block) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    for (uint32_t index = 0u; index < block_count; ++index) {
        const fv_block_result_t result = read_one(
            sd, first_block + index,
            output + (size_t)index * FV_BLOCK_SIZE);
        if (result != FV_BLOCK_OK) return result;
    }
    return FV_BLOCK_OK;
}

static fv_block_result_t write_blocks(fv_block_device_t *device,
                                      uint64_t first_block,
                                      uint32_t block_count,
                                      const uint8_t *input) {
    fv_rp2354_sd_t *sd = device != NULL ? device->context : NULL;
    if (sd == NULL || input == NULL || block_count == 0u) {
        return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    }
    if (!sd->initialized) return FV_BLOCK_ERROR_NOT_READY;
    if (!card_detected(sd)) {
        mark_not_ready(sd, false);
        return FV_BLOCK_ERROR_NOT_READY;
    }
    if (first_block >= sd->blocks ||
        block_count > sd->blocks - first_block) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    for (uint32_t index = 0u; index < block_count; ++index) {
        const fv_block_result_t result = write_one(
            sd, first_block + index,
            input + (size_t)index * FV_BLOCK_SIZE);
        if (result != FV_BLOCK_OK) return result;
    }
    return FV_BLOCK_OK;
}

static fv_block_result_t sync_blocks(fv_block_device_t *device) {
    fv_rp2354_sd_t *sd = device != NULL ? device->context : NULL;
    if (sd == NULL) return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if (!sd->initialized) return FV_BLOCK_ERROR_NOT_READY;
    if (!card_detected(sd)) {
        mark_not_ready(sd, false);
        return FV_BLOCK_ERROR_NOT_READY;
    }
    if (!select_card()) {
        deselect_card();
        mark_not_ready(sd, true);
        return FV_BLOCK_ERROR_IO;
    }
    deselect_card();
    return FV_BLOCK_OK;
}

static uint64_t block_count(const fv_block_device_t *device) {
    const fv_rp2354_sd_t *sd = device != NULL ? device->context : NULL;
    return sd != NULL && sd->initialized && card_detected(sd)
        ? sd->blocks : 0u;
}

static bool is_present(const fv_block_device_t *device) {
    const fv_rp2354_sd_t *sd = device != NULL ? device->context : NULL;
    return sd != NULL && sd->initialized && card_detected(sd);
}

static const fv_block_device_ops_t OPS = {
    .read = read_blocks,
    .write = write_blocks,
    .sync = sync_blocks,
    .block_count = block_count,
    .is_present = is_present,
};

bool fv_rp2354_sd_init(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return false;
    memset(sd, 0, sizeof(*sd));
    sd->interface.ops = &OPS;
    sd->interface.context = sd;

    gpio_init(FUSE_VAULT_SD_CLK_PIN);
    gpio_set_dir(FUSE_VAULT_SD_CLK_PIN, GPIO_OUT);
    gpio_put(FUSE_VAULT_SD_CLK_PIN, false);
    gpio_init(FUSE_VAULT_SD_CMD_PIN);
    gpio_set_dir(FUSE_VAULT_SD_CMD_PIN, GPIO_OUT);
    gpio_put(FUSE_VAULT_SD_CMD_PIN, true);
    gpio_pull_up(FUSE_VAULT_SD_CMD_PIN);
    gpio_init(FUSE_VAULT_SD_DAT0_PIN);
    gpio_set_dir(FUSE_VAULT_SD_DAT0_PIN, GPIO_IN);
    gpio_pull_up(FUSE_VAULT_SD_DAT0_PIN);
    gpio_init(FUSE_VAULT_SD_DAT1_PIN);
    gpio_set_dir(FUSE_VAULT_SD_DAT1_PIN, GPIO_IN);
    gpio_pull_up(FUSE_VAULT_SD_DAT1_PIN);
    gpio_init(FUSE_VAULT_SD_DAT2_PIN);
    gpio_set_dir(FUSE_VAULT_SD_DAT2_PIN, GPIO_IN);
    gpio_pull_up(FUSE_VAULT_SD_DAT2_PIN);
    gpio_init(FUSE_VAULT_SD_DAT3_PIN);
    gpio_set_dir(FUSE_VAULT_SD_DAT3_PIN, GPIO_OUT);
    gpio_put(FUSE_VAULT_SD_DAT3_PIN, true);
    gpio_pull_up(FUSE_VAULT_SD_DAT3_PIN);

#if FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
    gpio_init(FUSE_VAULT_SD_CARD_DETECT_PIN);
    gpio_set_dir(FUSE_VAULT_SD_CARD_DETECT_PIN, GPIO_IN);
    gpio_disable_pulls(FUSE_VAULT_SD_CARD_DETECT_PIN);
    sd->detect_configured = true;
#endif

    sd->physical_present = card_detected(sd);
    if (sd->physical_present && !initialize_card(sd)) {
        sd->failure_pending = true;
    }
    return true;
}

bool fv_rp2354_sd_reinitialize(fv_rp2354_sd_t *sd) {
    if (sd == NULL || sd->interface.ops != &OPS) return false;
    sd->physical_present = card_detected(sd);
    if (!sd->physical_present || !initialize_card(sd)) {
        sd->failure_pending = sd->physical_present;
        return false;
    }
    sd->insertion_pending = true;
    return true;
}

void fv_rp2354_sd_poll(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return;
    const bool present = card_detected(sd);
    if (sd->initialized && !present) {
        mark_not_ready(sd, false);
        sd->physical_present = false;
        deselect_card();
    } else if (!sd->initialized && !present) {
        sd->physical_present = false;
    } else if (!sd->initialized && present && !sd->physical_present) {
        sd->physical_present = true;
        if (initialize_card(sd)) sd->insertion_pending = true;
        else sd->failure_pending = true;
    }
}

bool fv_rp2354_sd_take_insertion_event(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return false;
    const bool pending = sd->insertion_pending;
    sd->insertion_pending = false;
    return pending;
}

bool fv_rp2354_sd_take_removal_event(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return false;
    const bool pending = sd->removal_pending;
    sd->removal_pending = false;
    return pending;
}

bool fv_rp2354_sd_take_failure_event(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return false;
    const bool pending = sd->failure_pending;
    sd->failure_pending = false;
    return pending;
}

void fv_rp2354_sd_deinit(fv_rp2354_sd_t *sd) {
    if (sd == NULL) return;
    mark_not_ready(sd, false);
    sd->physical_present = false;
    sd->insertion_pending = false;
    sd->removal_pending = false;
    sd->failure_pending = false;
    deselect_card();
}
