#include "fuse_vault/rp2354_otp.h"

#include "pico/bootrom.h"

#include <stddef.h>
#include <stdint.h>

static bool read_ecc_rows(fv_device_roots_storage_t *storage,
                          uint16_t first_row, uint16_t *output,
                          size_t row_count) {
    (void)storage;
    if (output == NULL || row_count > UINT32_MAX / sizeof(uint16_t)) return false;
    const otp_cmd_t command = {
        .flags = (uint32_t)first_row | OTP_CMD_ECC_BITS,
    };
    return rom_func_otp_access((uint8_t *)output,
                               (uint32_t)(row_count * sizeof(uint16_t)),
                               command) == BOOTROM_OK;
}

static bool write_ecc_rows(fv_device_roots_storage_t *storage,
                           uint16_t first_row, const uint16_t *input,
                           size_t row_count) {
    (void)storage;
    if (input == NULL || row_count > UINT32_MAX / sizeof(uint16_t)) return false;
    const otp_cmd_t command = {
        .flags = (uint32_t)first_row | OTP_CMD_ECC_BITS | OTP_CMD_WRITE_BITS,
    };
    return rom_func_otp_access((uint8_t *)(uintptr_t)input,
                               (uint32_t)(row_count * sizeof(uint16_t)),
                               command) == BOOTROM_OK;
}

static const fv_device_roots_storage_ops_t OTP_OPS = {
    .read_ecc_rows = read_ecc_rows,
    .write_ecc_rows = write_ecc_rows,
};

bool fv_rp2354_otp_init(fv_device_roots_storage_t *storage) {
    if (storage == NULL) return false;
    *storage = (fv_device_roots_storage_t) {
        .ops = &OTP_OPS,
        .context = NULL,
        /* First setup may create roots only in a completely EMPTY layout.
         * Active, partial, revoked and unreadable layouts are never replaced. */
        .root_programming_enabled = true,
    };
    return true;
}
