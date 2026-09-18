#define _POSIX_C_SOURCE 200809L

#include "otp_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static fv_device_secret_t sample_roots(void) {
    fv_device_secret_t roots;
    for (size_t index = 0u; index < sizeof(roots.device_secret); ++index) {
        roots.device_secret[index] = (uint8_t)(index + 1u);
    }
    return roots;
}

int main(void) {
    char directory[] = "/tmp/fuse-vault-otp-test.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[512];
    const int length = snprintf(path, sizeof(path), "%s/otp.bin", directory);
    CHECK(length > 0 && (size_t)length < sizeof(path));

    fv_host_otp_file_t first_file;
    fv_device_roots_storage_t first_storage;
    CHECK(fv_host_otp_file_init(&first_file, &first_storage, path, true));
    struct stat file_status;
    CHECK(stat(path, &file_status) == 0);
    CHECK(file_status.st_size == (off_t)FV_HOST_OTP_FILE_SIZE);
    CHECK(fv_device_roots_status(&first_storage) == FV_DEVICE_ROOTS_EMPTY);

    const fv_device_secret_t roots = sample_roots();
    CHECK(fv_device_roots_provision(&first_storage, &roots) ==
          FV_DEVICE_ROOTS_OK);

    /* Re-open the backend to model a power cycle/process restart. */
    fv_host_otp_file_t second_file;
    fv_device_roots_storage_t second_storage;
    CHECK(fv_host_otp_file_init(&second_file, &second_storage, path, false));
    CHECK(fv_device_roots_status(&second_storage) == FV_DEVICE_ROOTS_ACTIVE);
    fv_device_secret_t recovered;
    CHECK(fv_device_roots_read(&second_storage, &recovered) ==
          FV_DEVICE_ROOTS_ACTIVE);
    CHECK(memcmp(&roots, &recovered, sizeof(roots)) == 0);

    /* A direct request to clear a programmed bit must be rejected. */
    const uint16_t zero = 0u;
    CHECK(!second_storage.ops->write_ecc_rows(
        &second_storage, FV_DEVICE_ROOTS_FIRST_ROW, &zero, 1u));
    CHECK(fv_device_roots_status(&second_storage) == FV_DEVICE_ROOTS_ACTIVE);

    CHECK(fv_device_roots_revoke(&second_storage) == FV_DEVICE_ROOTS_OK);
    fv_host_otp_file_t third_file;
    fv_device_roots_storage_t third_storage;
    CHECK(fv_host_otp_file_init(&third_file, &third_storage, path, true));
    CHECK(fv_device_roots_status(&third_storage) == FV_DEVICE_ROOTS_REVOKED);
    CHECK(fv_device_roots_provision(&third_storage, &roots) ==
          FV_DEVICE_ROOTS_INVALID);

    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("All file-backed OTP tests passed.");
    return EXIT_SUCCESS;
}
