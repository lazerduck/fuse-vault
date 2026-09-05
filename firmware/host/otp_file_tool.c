#include "otp_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

static const char *result_name(fv_device_roots_result_t result) {
    switch (result) {
        case FV_DEVICE_ROOTS_OK: return "ok";
        case FV_DEVICE_ROOTS_EMPTY: return "empty";
        case FV_DEVICE_ROOTS_ACTIVE: return "active";
        case FV_DEVICE_ROOTS_REVOKED: return "revoked";
        case FV_DEVICE_ROOTS_INVALID: return "invalid";
        case FV_DEVICE_ROOTS_IO_ERROR: return "I/O error";
        case FV_DEVICE_ROOTS_NOT_PERMITTED: return "not permitted";
    }
    return "unknown";
}

static bool random_fill(uint8_t *output, size_t length) {
    size_t filled = 0u;
    while (filled < length) {
        const ssize_t result = getrandom(output + filled, length - filled, 0u);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        filled += (size_t)result;
    }
    return true;
}

static void clear_secret(fv_device_secret_t *secret) {
    volatile uint8_t *bytes = secret->device_secret;
    for (size_t index = 0u; index < sizeof(secret->device_secret); ++index) {
        bytes[index] = 0u;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s OTP_FILE status|provision|revoke\n", argv[0]);
        return EXIT_FAILURE;
    }
    fv_host_otp_file_t file;
    fv_device_roots_storage_t storage;
    const bool provisioning = strcmp(argv[2], "provision") == 0;
    if (!fv_host_otp_file_init(&file, &storage, argv[1], provisioning)) {
        fprintf(stderr, "Could not open emulated OTP file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    fv_device_roots_result_t result;
    if (strcmp(argv[2], "status") == 0) {
        result = fv_device_roots_status(&storage);
    } else if (strcmp(argv[2], "provision") == 0) {
        fv_device_secret_t roots;
        if (!random_fill(roots.device_secret, sizeof(roots.device_secret))) {
            fputs("Could not obtain host random data.\n", stderr);
            return EXIT_FAILURE;
        }
        result = fv_device_roots_provision(&storage, &roots);
        clear_secret(&roots);
    } else if (strcmp(argv[2], "revoke") == 0) {
        result = fv_device_roots_revoke(&storage);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[2]);
        return EXIT_FAILURE;
    }
    printf("%s: %s\n", argv[1], result_name(result));
    return result == FV_DEVICE_ROOTS_IO_ERROR ||
                   result == FV_DEVICE_ROOTS_INVALID ||
                   result == FV_DEVICE_ROOTS_NOT_PERMITTED
               ? EXIT_FAILURE : EXIT_SUCCESS;
}
