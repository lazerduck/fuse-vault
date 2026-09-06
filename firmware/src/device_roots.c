#include "fuse_vault/device_roots.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FV_ROOTS_FORMAT_MARKER UINT16_C(0x4656)
#define FV_ROOTS_ACTIVE_MARKER UINT16_C(0xa55a)
#define FV_ROOTS_REVOKED_MARKER UINT16_C(0xdead)
#define FV_OTP_PAGE_ROWS 64u
#define FV_ROOTS_TOTAL_ROWS (FV_OTP_PAGE_ROWS * 2u)
#define FV_REVOCATION_INDEX FV_OTP_PAGE_ROWS

_Static_assert(FV_DEVICE_ROOTS_ACTIVE_ROW <
                   (FV_DEVICE_ROOTS_OTP_PAGE + 1u) * 64u,
               "Device-root layout must fit inside one OTP page");
_Static_assert(FV_DEVICE_ROOTS_OTP_PAGE >= 3u &&
                   FV_DEVICE_ROOTS_OTP_PAGE <= 60u,
               "Device roots must use an RP2350 user-data OTP page");
_Static_assert(FV_DEVICE_REVOCATION_OTP_PAGE >= 3u &&
                   FV_DEVICE_REVOCATION_OTP_PAGE <= 60u &&
                   FV_DEVICE_REVOCATION_OTP_PAGE != FV_DEVICE_ROOTS_OTP_PAGE,
               "Revocation must use a separate RP2350 user-data OTP page");

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool storage_is_valid(const fv_device_roots_storage_t *storage) {
    return storage != NULL && storage->ops != NULL &&
           storage->ops->read_ecc_rows != NULL &&
           storage->ops->write_ecc_rows != NULL;
}

static bool all_zero(const uint16_t *rows, size_t count) {
    uint16_t combined = 0u;
    for (size_t index = 0u; index < count; ++index) combined |= rows[index];
    return combined == 0u;
}

static fv_device_roots_result_t inspect_rows(
    const uint16_t rows[FV_ROOTS_TOTAL_ROWS]) {
    if (rows[FV_REVOCATION_INDEX] == FV_ROOTS_REVOKED_MARKER) {
        return FV_DEVICE_ROOTS_REVOKED;
    }
    if (rows[FV_REVOCATION_INDEX] != 0u) return FV_DEVICE_ROOTS_INVALID;
    if (all_zero(rows, FV_ROOTS_TOTAL_ROWS)) return FV_DEVICE_ROOTS_EMPTY;
    if (rows[32] != FV_ROOTS_FORMAT_MARKER ||
        rows[33] != FV_ROOTS_ACTIVE_MARKER ||
        all_zero(rows, 16u) || all_zero(rows + 16u, 16u)) {
        return FV_DEVICE_ROOTS_INVALID;
    }
    if (!all_zero(rows + 34u, FV_OTP_PAGE_ROWS - 34u) ||
        !all_zero(rows + FV_REVOCATION_INDEX + 1u,
                  FV_OTP_PAGE_ROWS - 1u)) {
        return FV_DEVICE_ROOTS_INVALID;
    }
    return FV_DEVICE_ROOTS_ACTIVE;
}

static bool read_all(fv_device_roots_storage_t *storage,
                     uint16_t rows[FV_ROOTS_TOTAL_ROWS]) {
    return storage->ops->read_ecc_rows(storage, FV_DEVICE_ROOTS_FIRST_ROW,
                                       rows, FV_OTP_PAGE_ROWS) &&
           storage->ops->read_ecc_rows(storage, FV_DEVICE_ROOTS_REVOKED_ROW,
                                       rows + FV_REVOCATION_INDEX,
                                       FV_OTP_PAGE_ROWS);
}

fv_device_roots_result_t fv_device_roots_status(
    fv_device_roots_storage_t *storage) {
    if (!storage_is_valid(storage)) return FV_DEVICE_ROOTS_INVALID;
    uint16_t rows[FV_ROOTS_TOTAL_ROWS];
    if (!read_all(storage, rows)) return FV_DEVICE_ROOTS_IO_ERROR;
    const fv_device_roots_result_t result = inspect_rows(rows);
    secure_clear(rows, sizeof(rows));
    return result;
}

fv_device_roots_result_t fv_device_roots_read(
    fv_device_roots_storage_t *storage, fv_device_secret_t *roots) {
    if (!storage_is_valid(storage) || roots == NULL) {
        return FV_DEVICE_ROOTS_INVALID;
    }
    uint16_t rows[FV_ROOTS_TOTAL_ROWS];
    if (!read_all(storage, rows)) return FV_DEVICE_ROOTS_IO_ERROR;
    const fv_device_roots_result_t status = inspect_rows(rows);
    if (status == FV_DEVICE_ROOTS_ACTIVE) {
        for (size_t index = 0u; index < FV_DEVICE_ROOTS_DATA_ROWS; ++index) {
            roots->device_secret[index * 2u] = (uint8_t)rows[index];
            roots->device_secret[index * 2u + 1u] =
                (uint8_t)(rows[index] >> 8u);
        }
    } else {
        secure_clear(roots, sizeof(*roots));
    }
    secure_clear(rows, sizeof(rows));
    return status;
}

static bool write_and_verify(fv_device_roots_storage_t *storage,
                             uint16_t first_row, const uint16_t *input,
                             size_t count) {
    uint16_t verify[FV_DEVICE_ROOTS_DATA_ROWS];
    if (count > FV_DEVICE_ROOTS_DATA_ROWS ||
        !storage->ops->write_ecc_rows(storage, first_row, input, count) ||
        !storage->ops->read_ecc_rows(storage, first_row, verify, count)) {
        secure_clear(verify, sizeof(verify));
        return false;
    }
    const bool valid = memcmp(input, verify, count * sizeof(uint16_t)) == 0;
    secure_clear(verify, sizeof(verify));
    return valid;
}

fv_device_roots_result_t fv_device_roots_provision(
    fv_device_roots_storage_t *storage, const fv_device_secret_t *roots) {
    if (!storage_is_valid(storage) || roots == NULL) {
        return FV_DEVICE_ROOTS_INVALID;
    }
    if (!storage->root_programming_enabled) return FV_DEVICE_ROOTS_NOT_PERMITTED;
    const fv_device_roots_result_t initial_status = fv_device_roots_status(storage);
    if (initial_status == FV_DEVICE_ROOTS_IO_ERROR) return initial_status;
    if (initial_status != FV_DEVICE_ROOTS_EMPTY) return FV_DEVICE_ROOTS_INVALID;
    uint16_t root_rows[FV_DEVICE_ROOTS_DATA_ROWS];
    for (size_t index = 0u; index < FV_DEVICE_ROOTS_DATA_ROWS; ++index) {
        root_rows[index] = (uint16_t)roots->device_secret[index * 2u] |
            ((uint16_t)roots->device_secret[index * 2u + 1u] << 8u);
    }
    if (all_zero(root_rows, 16u) || all_zero(root_rows + 16u, 16u) ||
        !write_and_verify(storage, FV_DEVICE_ROOTS_FIRST_ROW, root_rows,
                          FV_DEVICE_ROOTS_DATA_ROWS)) {
        secure_clear(root_rows, sizeof(root_rows));
        return FV_DEVICE_ROOTS_IO_ERROR;
    }
    secure_clear(root_rows, sizeof(root_rows));
    const uint16_t format = FV_ROOTS_FORMAT_MARKER;
    if (!write_and_verify(storage, FV_DEVICE_ROOTS_FORMAT_ROW, &format, 1u)) {
        return FV_DEVICE_ROOTS_IO_ERROR;
    }
    const uint16_t active = FV_ROOTS_ACTIVE_MARKER;
    if (!write_and_verify(storage, FV_DEVICE_ROOTS_ACTIVE_ROW, &active, 1u)) {
        return FV_DEVICE_ROOTS_IO_ERROR;
    }
    return fv_device_roots_status(storage) == FV_DEVICE_ROOTS_ACTIVE
        ? FV_DEVICE_ROOTS_OK : FV_DEVICE_ROOTS_IO_ERROR;
}

fv_device_roots_result_t fv_device_roots_revoke(
    fv_device_roots_storage_t *storage) {
    if (!storage_is_valid(storage)) return FV_DEVICE_ROOTS_INVALID;
    const fv_device_roots_result_t initial_status = fv_device_roots_status(storage);
    if (initial_status == FV_DEVICE_ROOTS_IO_ERROR) return initial_status;
    if (initial_status == FV_DEVICE_ROOTS_REVOKED) {
        return FV_DEVICE_ROOTS_REVOKED;
    }
    const uint16_t revoked = FV_ROOTS_REVOKED_MARKER;
    if (!write_and_verify(storage, FV_DEVICE_ROOTS_REVOKED_ROW, &revoked, 1u)) {
        return FV_DEVICE_ROOTS_IO_ERROR;
    }
    return fv_device_roots_status(storage) == FV_DEVICE_ROOTS_REVOKED
        ? FV_DEVICE_ROOTS_OK : FV_DEVICE_ROOTS_IO_ERROR;
}
