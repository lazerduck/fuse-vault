#define _POSIX_C_SOURCE 200809L

#include "otp_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static bool write_all(int descriptor, const uint8_t *data, size_t length,
                      off_t offset) {
    size_t written = 0u;
    while (written < length) {
        const ssize_t result = pwrite(descriptor, data + written,
                                      length - written,
                                      offset + (off_t)written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        written += (size_t)result;
    }
    return true;
}

static bool read_all(int descriptor, uint8_t *data, size_t length,
                     off_t offset) {
    size_t consumed = 0u;
    while (consumed < length) {
        const ssize_t result = pread(descriptor, data + consumed,
                                     length - consumed,
                                     offset + (off_t)consumed);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        consumed += (size_t)result;
    }
    return true;
}

static bool create_blank_file(const char *path) {
    const int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                                S_IRUSR | S_IWUSR);
    if (descriptor < 0) return errno == EEXIST;
    const uint8_t zeroes[256] = {0};
    bool ok = true;
    for (size_t offset = 0u; ok && offset < FV_HOST_OTP_FILE_SIZE;
         offset += sizeof(zeroes)) {
        ok = write_all(descriptor, zeroes, sizeof(zeroes), (off_t)offset);
    }
    if (ok) ok = fsync(descriptor) == 0;
    if (close(descriptor) != 0) ok = false;
    if (!ok) (void)unlink(path);
    return ok;
}

static int open_validated(const fv_host_otp_file_t *file, int flags) {
    const int descriptor = open(file->path, flags | O_CLOEXEC);
    if (descriptor < 0) return -1;
    struct stat status;
    if (fstat(descriptor, &status) != 0 ||
        status.st_size != (off_t)FV_HOST_OTP_FILE_SIZE) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static bool rows_in_range(uint16_t first_row, size_t count) {
    return (size_t)first_row <= FV_HOST_OTP_ROW_COUNT &&
           count <= FV_HOST_OTP_ROW_COUNT - (size_t)first_row;
}

static bool read_rows(fv_device_roots_storage_t *storage, uint16_t first_row,
                      uint16_t *output, size_t count) {
    if (output == NULL || !rows_in_range(first_row, count)) return false;
    const fv_host_otp_file_t *file = storage->context;
    const int descriptor = open_validated(file, O_RDONLY);
    if (descriptor < 0) return false;
    bool ok = true;
    for (size_t index = 0u; ok && index < count; ++index) {
        uint8_t encoded[2] = {0};
        const off_t offset = (off_t)(((size_t)first_row + index) * 2u);
        ok = read_all(descriptor, encoded, sizeof(encoded), offset);
        if (ok) {
            output[index] = (uint16_t)((uint16_t)encoded[0] |
                            (uint16_t)((uint16_t)encoded[1] << 8u));
        }
    }
    if (close(descriptor) != 0) ok = false;
    return ok;
}

static bool write_rows(fv_device_roots_storage_t *storage, uint16_t first_row,
                       const uint16_t *input, size_t count) {
    if (input == NULL || !rows_in_range(first_row, count)) return false;
    const fv_host_otp_file_t *file = storage->context;
    const int descriptor = open_validated(file, O_RDWR);
    if (descriptor < 0) return false;
    bool ok = true;
    for (size_t index = 0u; ok && index < count; ++index) {
        uint8_t encoded[2] = {0};
        const off_t offset = (off_t)(((size_t)first_row + index) * 2u);
        ok = read_all(descriptor, encoded, sizeof(encoded), offset);
        const uint16_t existing = (uint16_t)((uint16_t)encoded[0] |
                                  (uint16_t)((uint16_t)encoded[1] << 8u));
        if (ok && (existing & input[index]) != existing) ok = false;
        const uint16_t programmed = existing | input[index];
        encoded[0] = (uint8_t)programmed;
        encoded[1] = (uint8_t)(programmed >> 8u);
        if (ok) ok = write_all(descriptor, encoded, sizeof(encoded), offset);
        /* Each logical row is made durable independently, like an OTP write. */
        if (ok) ok = fdatasync(descriptor) == 0;
    }
    if (close(descriptor) != 0) ok = false;
    return ok;
}

static const fv_device_roots_storage_ops_t FILE_OPS = {
    .read_ecc_rows = read_rows,
    .write_ecc_rows = write_rows,
};

bool fv_host_otp_file_init(fv_host_otp_file_t *file,
                           fv_device_roots_storage_t *storage,
                           const char *path, bool root_programming_enabled) {
    if (file == NULL || storage == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    const int length = snprintf(file->path, sizeof(file->path), "%s", path);
    if (length < 0 || (size_t)length >= sizeof(file->path)) return false;
    if (!create_blank_file(file->path)) return false;
    const int descriptor = open_validated(file, O_RDONLY);
    if (descriptor < 0) return false;
    if (close(descriptor) != 0) return false;
    *storage = (fv_device_roots_storage_t) {
        .ops = &FILE_OPS,
        .context = file,
        .root_programming_enabled = root_programming_enabled,
    };
    return true;
}
