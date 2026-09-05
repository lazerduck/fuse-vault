#define _POSIX_C_SOURCE 200809L

#include "fuse_vault/device_provisioning.h"
#include "fuse_vault/journal_authenticator.h"
#include "nor_flash.h"
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

static bool flash_read(fv_journal_flash_t *flash, size_t offset,
                       uint8_t *output, size_t length) {
    return fv_nor_read(flash->context, offset, output, length) == FV_NOR_OK;
}

static bool flash_program(fv_journal_flash_t *flash, size_t offset,
                          const uint8_t *input, size_t length) {
    return fv_nor_program(flash->context, offset, input, length) == FV_NOR_OK;
}

static bool flash_erase(fv_journal_flash_t *flash, size_t offset,
                        size_t length) {
    return fv_nor_erase(flash->context, offset, length) == FV_NOR_OK;
}

static const fv_journal_flash_ops_t FLASH_OPS = {
    .read = flash_read,
    .program = flash_program,
    .erase = flash_erase,
};

int main(void) {
    char directory[] = "/tmp/fuse-vault-provision-test.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char otp_path[512];
    CHECK(snprintf(otp_path, sizeof(otp_path), "%s/otp.bin", directory) > 0);
    fv_host_otp_file_t otp_file;
    fv_device_roots_storage_t roots_storage;
    CHECK(fv_host_otp_file_init(&otp_file, &roots_storage, otp_path, true));

    uint8_t flash_bytes[8192];
    fv_nor_flash_t nor;
    CHECK(fv_nor_init(&nor, flash_bytes, sizeof(flash_bytes), 4096u,
                      FV_JOURNAL_RECORD_SIZE));
    fv_journal_flash_t flash = {
        .ops = &FLASH_OPS,
        .context = &nor,
        .size = sizeof(flash_bytes),
        .erase_block_size = 4096u,
        .program_size = FV_JOURNAL_RECORD_SIZE,
    };
    const uint8_t device_context[FV_VAULT_ID_SIZE] = {
        'F', 'V', 'H', 'O', 'S', 'T', '0', '1'
    };
    random_fixture_t random = {0};
    const fv_device_provisioning_t provisioning = {
        .roots_storage = &roots_storage,
        .journal_flash = &flash,
        .device_context = device_context,
        .device_context_length = sizeof(device_context),
        .random_fill = deterministic_random,
        .random_context = &random,
    };
    fv_journal_state_t initial;
    CHECK(fv_device_provision(&provisioning, &initial) == FV_PROVISION_OK);
    CHECK(initial.sequence == 1u && initial.previous_sequence == 0u);
    CHECK(initial.provisioned && initial.failed_attempts == 0u);
    CHECK(fv_device_roots_status(&roots_storage) == FV_DEVICE_ROOTS_ACTIVE);
    const fv_journal_state_t expected = initial;
    fv_journal_state_t rejected_output;
    CHECK(fv_device_provision(&provisioning, &rejected_output) ==
          FV_PROVISION_NOT_EMPTY);
    const fv_journal_state_t empty_state = {0};
    CHECK(memcmp(&rejected_output, &empty_state, sizeof(empty_state)) == 0);

    /* Model a reboot and prove the stored roots authenticate stored state. */
    fv_host_otp_file_t reopened_file;
    fv_device_roots_storage_t reopened_roots;
    CHECK(fv_host_otp_file_init(&reopened_file, &reopened_roots, otp_path,
                                false));
    fv_device_secret_t roots;
    CHECK(fv_device_roots_read(&reopened_roots, &roots) ==
          FV_DEVICE_ROOTS_ACTIVE);
    fv_dual_journal_authenticator_t authenticator;
    CHECK(fv_dual_journal_authenticator_init(&authenticator, &roots,
                                             device_context));
    memset(&roots, 0, sizeof(roots));
    fv_security_journal_t journal;
    CHECK(fv_security_journal_init(&journal, &flash,
                                   &authenticator.interface));
    fv_journal_state_t recovered;
    CHECK(fv_security_journal_recover(&journal, &recovered) == FV_JOURNAL_OK);
    CHECK(recovered.sequence == expected.sequence);
    CHECK(memcmp(recovered.vault_id, expected.vault_id,
                 sizeof(expected.vault_id)) == 0);
    fv_dual_journal_authenticator_deinit(&authenticator);

    CHECK(unlink(otp_path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("All device provisioning tests passed.");
    return EXIT_SUCCESS;
}
