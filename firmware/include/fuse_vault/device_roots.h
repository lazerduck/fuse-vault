#ifndef FUSE_VAULT_DEVICE_ROOTS_H
#define FUSE_VAULT_DEVICE_ROOTS_H

#include "fuse_vault/persistence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_DEVICE_ROOTS_OTP_PAGE 60u
#define FV_DEVICE_REVOCATION_OTP_PAGE 59u
#define FV_DEVICE_ROOTS_FIRST_ROW (FV_DEVICE_ROOTS_OTP_PAGE * 64u)
#define FV_DEVICE_ROOTS_DATA_ROWS 32u
#define FV_DEVICE_ROOTS_FORMAT_ROW (FV_DEVICE_ROOTS_FIRST_ROW + 32u)
#define FV_DEVICE_ROOTS_ACTIVE_ROW (FV_DEVICE_ROOTS_FIRST_ROW + 33u)
#define FV_DEVICE_ROOTS_REVOKED_ROW (FV_DEVICE_REVOCATION_OTP_PAGE * 64u)

typedef enum {
    FV_DEVICE_ROOTS_OK = 0,
    FV_DEVICE_ROOTS_EMPTY,
    FV_DEVICE_ROOTS_ACTIVE,
    FV_DEVICE_ROOTS_REVOKED,
    FV_DEVICE_ROOTS_INVALID,
    FV_DEVICE_ROOTS_IO_ERROR,
    FV_DEVICE_ROOTS_NOT_PERMITTED,
} fv_device_roots_result_t;

typedef struct fv_device_roots_storage fv_device_roots_storage_t;

typedef struct {
    bool (*read_ecc_rows)(fv_device_roots_storage_t *storage, uint16_t first_row,
                          uint16_t *output, size_t row_count);
    bool (*write_ecc_rows)(fv_device_roots_storage_t *storage, uint16_t first_row,
                           const uint16_t *input, size_t row_count);
} fv_device_roots_storage_ops_t;

struct fv_device_roots_storage {
    const fv_device_roots_storage_ops_t *ops;
    void *context;
    bool root_programming_enabled;
};

fv_device_roots_result_t fv_device_roots_status(
    fv_device_roots_storage_t *storage);
fv_device_roots_result_t fv_device_roots_read(
    fv_device_roots_storage_t *storage, fv_device_secret_t *roots);
fv_device_roots_result_t fv_device_roots_provision(
    fv_device_roots_storage_t *storage, const fv_device_secret_t *roots);
fv_device_roots_result_t fv_device_roots_revoke(
    fv_device_roots_storage_t *storage);

#endif
