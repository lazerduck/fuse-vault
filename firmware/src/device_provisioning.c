#include "fuse_vault/device_provisioning.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool all_zero(const uint8_t *data, size_t length) {
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= data[index];
    return combined == 0u;
}

fv_provision_result_t fv_device_provision(
    const fv_device_provisioning_t *provisioning) {
    if (provisioning == NULL || provisioning->roots_storage == NULL ||
        provisioning->random_fill == NULL) {
        return FV_PROVISION_INVALID_ARGUMENT;
    }
    const fv_device_roots_result_t initial_status =
        fv_device_roots_status(provisioning->roots_storage);
    if (initial_status == FV_DEVICE_ROOTS_IO_ERROR) {
        return FV_PROVISION_ROOTS_FAILED;
    }
    if (initial_status != FV_DEVICE_ROOTS_EMPTY) {
        return FV_PROVISION_NOT_EMPTY;
    }

    fv_device_secret_t generated = {0};
    fv_device_secret_t verified = {0};
    fv_provision_result_t result = FV_PROVISION_RANDOM_FAILED;
    if (!provisioning->random_fill(
            provisioning->random_context, generated.device_secret,
            sizeof(generated.device_secret)) ||
        all_zero(generated.device_secret, FV_DEVICE_ROOT_SIZE) ||
        all_zero(generated.device_secret + FV_DEVICE_ROOT_SIZE,
                 FV_DEVICE_ROOT_SIZE)) {
        goto cleanup;
    }

    const fv_device_roots_result_t provision_result =
        fv_device_roots_provision(provisioning->roots_storage, &generated);
    if (provision_result != FV_DEVICE_ROOTS_OK &&
        fv_device_roots_status(provisioning->roots_storage) !=
            FV_DEVICE_ROOTS_ACTIVE) {
        result = FV_PROVISION_ROOTS_FAILED;
        goto cleanup;
    }
    if (fv_device_roots_read(provisioning->roots_storage, &verified) !=
            FV_DEVICE_ROOTS_ACTIVE ||
        memcmp(&generated, &verified, sizeof(generated)) != 0) {
        result = FV_PROVISION_VERIFICATION_FAILED;
        goto cleanup;
    }
    result = FV_PROVISION_OK;

cleanup:
    secure_clear(&generated, sizeof(generated));
    secure_clear(&verified, sizeof(verified));
    return result;
}
