#define _POSIX_C_SOURCE 200809L

#include "fuse_vault/device_provisioning.h"
#include "otp_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    uint8_t next;
    bool fail;
} random_fixture_t;

static bool deterministic_random(void *context, uint8_t *output,
                                 size_t length) {
    random_fixture_t *random = context;
    if (random->fail) return false;
    for (size_t index = 0u; index < length; ++index) {
        output[index] = ++random->next;
    }
    return true;
}

int main(void) {
    char directory[] = "/tmp/fuse-vault-provision-test.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char otp_path[512];
    CHECK(snprintf(otp_path, sizeof(otp_path), "%s/otp.bin", directory) > 0);
    fv_host_otp_file_t otp_file;
    fv_device_roots_storage_t roots_storage;
    CHECK(fv_host_otp_file_init(&otp_file, &roots_storage, otp_path, true));

    random_fixture_t random = {0};
    const fv_device_provisioning_t provisioning = {
        .roots_storage = &roots_storage,
        .random_fill = deterministic_random,
        .random_context = &random,
    };
    CHECK(fv_device_provision(&provisioning) == FV_PROVISION_OK);
    CHECK(fv_device_roots_status(&roots_storage) == FV_DEVICE_ROOTS_ACTIVE);
    CHECK(fv_device_provision(&provisioning) == FV_PROVISION_NOT_EMPTY);

    fv_host_otp_file_t reopened_file;
    fv_device_roots_storage_t reopened_roots;
    CHECK(fv_host_otp_file_init(&reopened_file, &reopened_roots, otp_path,
                                false));
    fv_device_secret_t roots;
    CHECK(fv_device_roots_read(&reopened_roots, &roots) ==
          FV_DEVICE_ROOTS_ACTIVE);
    uint8_t combined = 0u;
    for (size_t index = 0u; index < sizeof(roots.device_secret); ++index) {
        combined |= roots.device_secret[index];
    }
    CHECK(combined != 0u);
    memset(&roots, 0, sizeof(roots));

    CHECK(unlink(otp_path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("All device identity provisioning tests passed.");
    return EXIT_SUCCESS;
}
