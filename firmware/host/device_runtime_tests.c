#define _GNU_SOURCE

#include "host_services.h"

#include "fuse_vault/crypto_stack.h"
#include "fuse_vault/device_runtime.h"
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
    fv_virtual_msc_t msc;
    unsigned attach_count;
    unsigned detach_count;
} test_usb_t;

static bool attach_msc(void *context, fv_block_device_t *plaintext_blocks) {
    test_usb_t *usb = context;
    ++usb->attach_count;
    return fv_virtual_msc_attach(&usb->msc, plaintext_blocks);
}

static bool detach_usb(void *context) {
    test_usb_t *usb = context;
    ++usb->detach_count;
    if (usb->msc.attached) {
        fv_virtual_msc_block_requests(&usb->msc);
        fv_virtual_msc_detach(&usb->msc);
    }
    return true;
}

static bool attach_fido(void *context) {
    (void)context;
    return false;
}

static const fv_runtime_usb_ops_t USB_OPS = {
    .attach_msc = attach_msc,
    .detach_usb = detach_usb,
    .attach_fido = attach_fido,
};

static void set_wheel_secret(fv_secret_entry_t *entry, bool correct) {
    memset(entry, 0, sizeof(*entry));
    entry->method = FV_ENTRY_METHOD_WHEELS;
    entry->state.wheels.values[0] = 12u;
    entry->state.wheels.values[1] = 34u;
    entry->state.wheels.values[2] = correct ? 56u : 57u;
}

static void make_path(char output[4096], const char *directory,
                      const char *name) {
    const int result = snprintf(output, 4096u, "%s/%s", directory, name);
    CHECK(result > 0 && result < 4096);
}

static void cleanup(const char *directory) {
    static const char *const names[] = {
        "device-secret.0", "device-secret.1",
        "device-secret-active.0", "device-secret-active.1",
        "device-secret-revoked.0", "device-secret-revoked.1",
        "security-journal.bin", "vault-media.bin",
    };
    char path[4096];
    for (size_t index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        make_path(path, directory, names[index]);
        (void)unlink(path);
    }
    CHECK(rmdir(directory) == 0);
}

static void end_to_end_runtime(void) {
    char directory[] = "/tmp/fuse-vault-runtime-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);

    fv_platform_services_t services;
    fv_host_services_context_t services_context;
    CHECK(fv_host_services_init(&services, &services_context, directory));

    const fv_credential_costs_t costs = {
        .pbkdf2_iterations = 1u,
        .kmac_iterations = 1u,
    };
    test_usb_t usb = {0};
    fv_virtual_msc_init(&usb.msc);

    fv_app_t setup_app;
    fv_app_init(&setup_app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    fv_device_runtime_t setup_runtime;
    CHECK(fv_device_runtime_init(
        &setup_runtime, &setup_app, &services, &services_context.vault_device,
        &USB_OPS, &usb, &costs));
    fv_device_runtime_handle_event(&setup_runtime, FV_EVENT_BOOT_COMPLETED);
    CHECK(setup_app.state == FV_STATE_SETUP_REQUIRED);
    fv_device_runtime_handle_event(&setup_runtime, FV_EVENT_SELECT);
    CHECK(setup_app.state == FV_STATE_SETUP_MEDIA_CONFIRM);
    fv_device_runtime_handle_event(&setup_runtime, FV_EVENT_SELECT);
    CHECK(setup_app.state == FV_STATE_SETUP_METHOD_SELECT);

    setup_app.state = FV_STATE_PROVISIONING;
    set_wheel_secret(&setup_app.setup_secret_entry, true);
    setup_app.selected_stack_preset = 3u;
    CHECK(fv_crypto_stack_preset(setup_app.selected_stack_preset,
                                 &setup_app.selected_encryption_stack));

    fv_device_runtime_execute(&setup_runtime,
                              FV_COMMAND_BEGIN_PROVISIONING);
    CHECK(setup_app.state == FV_STATE_MODE_SELECT);
    CHECK(setup_app.provisioned);
    CHECK(setup_runtime.authentication.vmk_valid == false);
    CHECK(setup_runtime.encrypted_ready == false);
    fv_device_runtime_shutdown(&setup_runtime);

    fv_platform_services_t restarted_services;
    fv_host_services_context_t restarted_context;
    CHECK(fv_host_services_init(&restarted_services, &restarted_context,
                                directory));
    fv_security_state_t persisted;
    CHECK(restarted_services.ops->load_security_state(&restarted_services,
                                                      &persisted) ==
          FV_PERSIST_OK);

    fv_app_t app;
    fv_app_init(&app, persisted.provisioned, persisted.failed_attempts,
                FV_ENTRY_METHOD_WHEELS);
    app.state = FV_STATE_VAULT_SECRET_ENTRY;
    fv_device_runtime_t runtime;
    CHECK(fv_device_runtime_init(
        &runtime, &app, &restarted_services, &restarted_context.vault_device,
        &USB_OPS, &usb, &costs));

    set_wheel_secret(&app.secret_entry, false);
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_VAULT_SECRET_ENTRY);
    CHECK(app.failed_attempts == 1u);
    CHECK(!runtime.authentication.vmk_valid);
    CHECK(!runtime.encrypted_ready);
    CHECK(!usb.msc.attached);
    CHECK(restarted_services.ops->load_security_state(&restarted_services,
                                                      &persisted) ==
          FV_PERSIST_OK);
    CHECK(persisted.failed_attempts == 1u);

    set_wheel_secret(&app.secret_entry, true);
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    CHECK(app.state == FV_STATE_VAULT_UNLOCKED);
    CHECK(app.failed_attempts == 0u);
    CHECK(runtime.authentication.vmk_valid);
    CHECK(runtime.encrypted_ready);
    CHECK(usb.msc.attached);
    CHECK(usb.attach_count == 1u);

    uint8_t plaintext[FV_BLOCK_SIZE] = {0};
    uint8_t recovered[FV_BLOCK_SIZE] = {0};
    memcpy(plaintext, "runtime-coordinator plaintext", 29u);
    CHECK(fv_virtual_msc_write10(&usb.msc, 3u, 1u, plaintext) == FV_MSC_OK);
    CHECK(fv_virtual_msc_synchronize_cache(&usb.msc) == FV_MSC_OK);
    CHECK(fv_virtual_msc_read10(&usb.msc, 3u, 1u, recovered) == FV_MSC_OK);
    CHECK(memcmp(plaintext, recovered, sizeof(plaintext)) == 0);

    fv_device_runtime_handle_event(&runtime, FV_EVENT_BACK);
    CHECK(app.state == FV_STATE_MODE_SELECT);
    CHECK(!usb.msc.attached);
    CHECK(!runtime.authentication.vmk_valid);
    CHECK(!runtime.encrypted_ready);
    CHECK(fv_virtual_msc_read10(&usb.msc, 3u, 1u, recovered) ==
          FV_MSC_NOT_READY);

    fv_device_runtime_shutdown(&runtime);
    cleanup(directory);
}

int main(void) {
    end_to_end_runtime();
    puts("Device runtime end-to-end tests passed.");
    return EXIT_SUCCESS;
}
