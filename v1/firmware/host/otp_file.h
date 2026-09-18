#ifndef FUSE_VAULT_HOST_OTP_FILE_H
#define FUSE_VAULT_HOST_OTP_FILE_H

#include "fuse_vault/device_roots.h"

#include <stdbool.h>

#define FV_HOST_OTP_ROW_COUNT 4096u
#define FV_HOST_OTP_FILE_SIZE (FV_HOST_OTP_ROW_COUNT * 2u)
#define FV_HOST_OTP_PATH_CAPACITY 4096u

typedef struct {
    char path[FV_HOST_OTP_PATH_CAPACITY];
} fv_host_otp_file_t;

/*
 * Opens or creates an 8 KiB file containing 4096 little-endian ECC data rows.
 * A new file is all zeroes. Existing files must have exactly the expected size.
 */
bool fv_host_otp_file_init(fv_host_otp_file_t *file,
                           fv_device_roots_storage_t *storage,
                           const char *path, bool root_programming_enabled);

#endif
