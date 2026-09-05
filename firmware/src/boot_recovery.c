#include "fuse_vault/boot_recovery.h"

#include "fuse_vault/credential_envelope.h"

#include <stddef.h>
#include <stdint.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

fv_boot_recovery_result_t fv_boot_recover_entry_method(
    fv_platform_services_t *services, fv_entry_method_t *entry_method) {
    if (entry_method == NULL) return FV_BOOT_RECOVERY_INVALID;
    if (services == NULL || services->ops == NULL ||
        services->ops->load_vault_header == NULL) {
        return FV_BOOT_RECOVERY_UNAVAILABLE;
    }

    fv_vault_header_t header = {0};
    const fv_persist_result_t load_result =
        services->ops->load_vault_header(services, &header);
    if (load_result != FV_PERSIST_OK) {
        secure_clear(&header, sizeof(header));
        return load_result == FV_PERSIST_NOT_FOUND
            ? FV_BOOT_RECOVERY_UNAVAILABLE
            : FV_BOOT_RECOVERY_INVALID;
    }

    const bool valid = fv_vault_header_valid(&header) &&
        fv_secret_method_to_entry_method(header.entry_method, entry_method);
    secure_clear(&header, sizeof(header));
    return valid ? FV_BOOT_RECOVERY_OK : FV_BOOT_RECOVERY_INVALID;
}
