#define _GNU_SOURCE

#include "host_services.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void check(bool condition, const char *expression, const char *file,
                  int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

static void remove_test_directory(const char *directory) {
    char path[FV_HOST_PATH_CAPACITY];
    const char *const files[] = {
        "device-state.0", "device-state.1",
        "vault-header.0", "vault-header.1",
    };
    for (size_t index = 0u; index < sizeof(files) / sizeof(files[0]); ++index) {
        const int result = snprintf(path, sizeof(path), "%s/%s",
                                    directory, files[index]);
        if (result >= 0 && (size_t)result < sizeof(path)) (void)unlink(path);
    }
    (void)rmdir(directory);
}

int main(void) {
    char directory[] = "/tmp/fuse-vault-host-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);

    fv_platform_services_t services;
    fv_host_services_context_t context;
    CHECK(fv_host_services_init(&services, &context, directory));

    uint8_t random_bytes[64] = {0};
    CHECK(services.ops->random_fill(&services, random_bytes,
                                    sizeof(random_bytes)));

    fv_device_state_t missing_state;
    CHECK(services.ops->load_device_state(&services, &missing_state) ==
          FV_PERSIST_NOT_FOUND);

    fv_device_state_t state = {
        .sequence = 1u,
        .failed_attempts = 3u,
        .provisioned = true,
    };
    memcpy(state.device_secret, random_bytes, FV_DEVICE_SECRET_SIZE);
    CHECK(services.ops->store_device_state(&services, &state) == FV_PERSIST_OK);

    fv_device_state_t loaded;
    CHECK(services.ops->load_device_state(&services, &loaded) == FV_PERSIST_OK);
    CHECK(loaded.sequence == 1u);
    CHECK(loaded.failed_attempts == 3u);
    CHECK(loaded.provisioned);
    CHECK(memcmp(loaded.device_secret, state.device_secret,
                 FV_DEVICE_SECRET_SIZE) == 0);

    state.sequence = 2u;
    state.failed_attempts = 4u;
    CHECK(services.ops->store_device_state(&services, &state) == FV_PERSIST_OK);
    CHECK(services.ops->load_device_state(&services, &loaded) == FV_PERSIST_OK);
    CHECK(loaded.sequence == 2u);
    CHECK(loaded.failed_attempts == 4u);

    char newest_path[FV_HOST_PATH_CAPACITY];
    CHECK(snprintf(newest_path, sizeof(newest_path), "%s/device-state.0",
                   directory) > 0);
    const int descriptor = open(newest_path, O_WRONLY | O_TRUNC);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "corrupt", 7u) == 7);
    CHECK(close(descriptor) == 0);
    CHECK(services.ops->load_device_state(&services, &loaded) == FV_PERSIST_OK);
    CHECK(loaded.sequence == 1u);
    CHECK(loaded.failed_attempts == 3u);

    fv_vault_header_t header = {
        .sequence = 7u,
        .crypto_profile = FV_CRYPTO_PROFILE_DUAL_FAMILY_V1,
        .entry_method = 1u,
        .branch_a_cost = 1024u,
        .branch_b_cost = 2048u,
        .wrapped_vmk_length = 64u,
    };
    memcpy(header.vault_id, random_bytes, FV_VAULT_ID_SIZE);
    memcpy(header.branch_a_salt, random_bytes + 16u, FV_SALT_SIZE);
    memcpy(header.branch_b_salt, random_bytes + 32u, FV_SALT_SIZE);
    memcpy(header.wrapped_vmk, random_bytes, header.wrapped_vmk_length);
    CHECK(services.ops->store_vault_header(&services, &header) == FV_PERSIST_OK);

    fv_vault_header_t loaded_header;
    CHECK(services.ops->load_vault_header(&services, &loaded_header) ==
          FV_PERSIST_OK);
    CHECK(loaded_header.sequence == header.sequence);
    CHECK(loaded_header.crypto_profile == header.crypto_profile);
    CHECK(loaded_header.wrapped_vmk_length == header.wrapped_vmk_length);
    CHECK(memcmp(loaded_header.wrapped_vmk, header.wrapped_vmk,
                 header.wrapped_vmk_length) == 0);

    header.crypto_profile = FV_CRYPTO_PROFILE_UNAVAILABLE;
    CHECK(services.ops->store_vault_header(&services, &header) ==
          FV_PERSIST_INVALID);

    remove_test_directory(directory);
    puts("All Fuse Vault host service tests passed.");
    return EXIT_SUCCESS;
}
