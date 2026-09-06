#define _GNU_SOURCE

#include "file_block_device.h"
#include "nor_flash.h"
#include "otp_file.h"

#include "fuse_vault/device_runtime.h"
#include "fuse_vault/device_services.h"
#include "fuse_vault/virtual_msc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    uint8_t next;
} random_context_t;

static bool deterministic_random(void *context, uint8_t *output,
                                 size_t length) {
    random_context_t *random = context;
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

typedef struct {
    fv_virtual_msc_t msc;
} usb_context_t;

static bool attach_msc(void *context, fv_block_device_t *blocks) {
    return fv_virtual_msc_attach(&((usb_context_t *)context)->msc, blocks);
}

static bool detach_usb(void *context) {
    fv_virtual_msc_detach(&((usb_context_t *)context)->msc);
    return true;
}

static const fv_runtime_usb_ops_t USB_OPS = {
    .attach_msc = attach_msc,
    .detach_usb = detach_usb,
    .attach_fido = NULL,
};

static void set_secret(fv_secret_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));
    entry->method = FV_ENTRY_METHOD_WHEELS;
    entry->state.wheels.values[0] = 7u;
    entry->state.wheels.values[1] = 41u;
    entry->state.wheels.values[2] = 93u;
}

static void test_setup_and_restart(bool preprovisioned) {
    char directory[] = "/tmp/fuse-vault-device-services-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char otp_path[4096];
    char media_path[4096];
    CHECK(snprintf(otp_path, sizeof(otp_path), "%s/otp.bin", directory) > 0);
    CHECK(snprintf(media_path, sizeof(media_path), "%s/media.bin", directory) >
          0);

    fv_host_otp_file_t otp_file;
    fv_device_roots_storage_t roots_storage;
    CHECK(fv_host_otp_file_init(&otp_file, &roots_storage, otp_path, true));
    fv_device_secret_t factory_roots;
    for (size_t index = 0u; index < sizeof(factory_roots.device_secret);
         ++index) {
        factory_roots.device_secret[index] = (uint8_t)(index + 1u);
    }
    if (preprovisioned) {
        CHECK(fv_device_roots_provision(&roots_storage, &factory_roots) ==
              FV_DEVICE_ROOTS_OK);
    } else {
        CHECK(fv_device_roots_status(&roots_storage) == FV_DEVICE_ROOTS_EMPTY);
    }
    memset(&factory_roots, 0, sizeof(factory_roots));

    uint8_t flash_bytes[8192];
    fv_nor_flash_t nor;
    CHECK(fv_nor_init(&nor, flash_bytes, sizeof(flash_bytes), 4096u,
                      FV_JOURNAL_RECORD_SIZE));
    fv_journal_flash_t journal_flash = {
        .ops = &FLASH_OPS,
        .context = &nor,
        .size = sizeof(flash_bytes),
        .erase_block_size = 4096u,
        .program_size = FV_JOURNAL_RECORD_SIZE,
    };
    fv_block_device_t media;
    fv_host_file_block_context_t media_context;
    CHECK(fv_host_file_block_device_init(&media, &media_context, media_path,
                                         4096u));

    static const uint8_t device_id[FV_VAULT_ID_SIZE] = {
        'F','V','D','E','V','S','V','C','T','E','S','T','0','0','0','1'
    };
    random_context_t random = {0};
    fv_platform_services_t services;
    fv_device_services_context_t service_context;
    CHECK(fv_device_services_init(
        &services, &service_context, &roots_storage, &journal_flash, &media,
        device_id, deterministic_random, &random));

    const fv_credential_costs_t costs = {1u, 1u};
    usb_context_t usb = {0};
    fv_virtual_msc_init(&usb.msc);
    fv_app_t setup_app;
    fv_app_init(&setup_app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    setup_app.state = FV_STATE_PROVISIONING;
    set_secret(&setup_app.setup_secret_entry);
    fv_device_runtime_t setup_runtime;
    CHECK(fv_device_runtime_init(&setup_runtime, &setup_app, &services, &media,
                                 &USB_OPS, &usb, &costs));
    fv_device_runtime_execute(&setup_runtime, FV_COMMAND_BEGIN_PROVISIONING);
    CHECK(setup_app.state == FV_STATE_MODE_SELECT);
    CHECK(fv_device_roots_status(&roots_storage) == FV_DEVICE_ROOTS_ACTIVE);
    fv_device_secret_t initial_roots;
    CHECK(fv_device_roots_read(&roots_storage, &initial_roots) ==
          FV_DEVICE_ROOTS_ACTIVE);
    fv_device_runtime_shutdown(&setup_runtime);
    /* Reopen with the same programming capability: no second firmware image. */
    CHECK(fv_host_otp_file_init(&otp_file, &roots_storage, otp_path, true));

    fv_platform_services_t restarted_services;
    fv_device_services_context_t restarted_context;
    CHECK(fv_device_services_init(
        &restarted_services, &restarted_context, &roots_storage, &journal_flash,
        &media, device_id, deterministic_random, &random));
    fv_security_state_t state;
    CHECK(restarted_services.ops->load_security_state(&restarted_services,
                                                      &state) == FV_PERSIST_OK);
    CHECK(state.provisioned && state.failed_attempts == 0u);
    fv_vault_header_t header;
    CHECK(restarted_services.ops->load_vault_header(&restarted_services,
                                                    &header) == FV_PERSIST_OK);

    fv_app_t app;
    fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS);
    app.state = FV_STATE_VAULT_SECRET_ENTRY;
    set_secret(&app.secret_entry);
    fv_device_runtime_t runtime;
    CHECK(fv_device_runtime_init(&runtime, &app, &restarted_services, &media,
                                 &USB_OPS, &usb, &costs));
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_VAULT_UNLOCKED && usb.msc.attached);

    uint8_t input[FV_BLOCK_SIZE] = {0};
    uint8_t output[FV_BLOCK_SIZE] = {0};
    memcpy(input, "device-services target composition", 35u);
    CHECK(fv_virtual_msc_write10(&usb.msc, 8u, 1u, input) == FV_MSC_OK);
    CHECK(fv_virtual_msc_read10(&usb.msc, 8u, 1u, output) == FV_MSC_OK);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    fv_device_runtime_shutdown(&runtime);

    fv_device_secret_t recovered_roots;
    CHECK(fv_device_roots_read(&roots_storage, &recovered_roots) ==
          FV_DEVICE_ROOTS_ACTIVE);
    CHECK(memcmp(&initial_roots, &recovered_roots, sizeof(initial_roots)) == 0);
    memset(&initial_roots, 0, sizeof(initial_roots));
    memset(&recovered_roots, 0, sizeof(recovered_roots));
    CHECK(unlink(media_path) == 0);
    CHECK(unlink(otp_path) == 0);
    CHECK(rmdir(directory) == 0);
}

int main(void) {
    test_setup_and_restart(false);
    test_setup_and_restart(true);
    puts("Device services composition tests passed.");
    return EXIT_SUCCESS;
}
