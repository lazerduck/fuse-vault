#define _GNU_SOURCE

#include "fuse_vault/app.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/credential_envelope.h"
#include "host_services.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void check(bool condition, const char *expression, const char *file,
                  int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

static fv_vault_header_t valid_header(fv_secret_method_t method) {
    fv_vault_header_t header = {
        .sequence = 1u,
        .crypto_profile = FV_CRYPTO_PROFILE_DUAL_FAMILY_V1,
        .entry_method = method,
        .branch_a_cost = 1u,
        .branch_b_cost = 1u,
        .wrapped_vmk_length = FV_CREDENTIAL_ENVELOPE_SIZE,
    };
    memset(header.vault_id, 0x11, sizeof(header.vault_id));
    memset(header.branch_a_salt, 0x22, sizeof(header.branch_a_salt));
    memset(header.branch_b_salt, 0x33, sizeof(header.branch_b_salt));
    memset(header.wrapped_vmk, 0x44, header.wrapped_vmk_length);
    return header;
}

static void remove_state_directory(const char *directory) {
    char path[FV_HOST_PATH_CAPACITY];
    for (unsigned slot = 0u; slot < 2u; ++slot) {
        const int result = snprintf(path, sizeof(path), "%s/vault-header.%u",
                                    directory, slot);
        if (result > 0 && (size_t)result < sizeof(path)) (void)unlink(path);
    }
    (void)rmdir(directory);
}

static void test_all_methods_survive_restart(void) {
    for (fv_entry_method_t expected = FV_ENTRY_METHOD_WHEELS;
         expected < FV_ENTRY_METHOD_COUNT;
         expected = (fv_entry_method_t)(expected + 1)) {
        char directory[] = "/tmp/fuse-vault-recovery-test-XXXXXX";
        CHECK(mkdtemp(directory) != NULL);
        fv_platform_services_t writer;
        fv_host_services_context_t writer_context;
        CHECK(fv_host_services_init(&writer, &writer_context, directory));

        fv_secret_method_t stable_method;
        CHECK(fv_entry_method_to_secret_method(expected, &stable_method));
        const fv_vault_header_t header = valid_header(stable_method);
        CHECK(writer.ops->store_vault_header(&writer, &header) == FV_PERSIST_OK);

        /* A fresh service object models a process/device restart. */
        fv_platform_services_t reader;
        fv_host_services_context_t reader_context;
        CHECK(fv_host_services_init(&reader, &reader_context, directory));
        fv_entry_method_t recovered = FV_ENTRY_METHOD_COUNT;
        CHECK(fv_boot_recover_entry_method(&reader, &recovered) ==
              FV_BOOT_RECOVERY_OK);
        CHECK(recovered == expected);
        fv_app_t restarted_app;
        fv_app_init(&restarted_app, true, 0u, recovered);
        (void)fv_app_handle(&restarted_app, FV_EVENT_BOOT_COMPLETED);
        CHECK(restarted_app.state == FV_STATE_MODE_SELECT);
        (void)fv_app_handle(&restarted_app, FV_EVENT_SELECT);
        CHECK(restarted_app.state == FV_STATE_VAULT_SECRET_ENTRY);
        CHECK(restarted_app.secret_entry.method == expected);
        remove_state_directory(directory);
    }
}

typedef struct {
    fv_vault_header_t header;
    fv_persist_result_t result;
} fake_context_t;

static fv_persist_result_t fake_load(fv_platform_services_t *services,
                                     fv_vault_header_t *header) {
    const fake_context_t *context = services->context;
    if (context->result == FV_PERSIST_OK) *header = context->header;
    return context->result;
}

static const fv_platform_service_ops_t FAKE_OPS = {
    .load_vault_header = fake_load,
};

static fv_state_t boot_with(fake_context_t *context) {
    fv_platform_services_t services = {
        .ops = &FAKE_OPS,
        .context = context,
    };
    fv_entry_method_t method = FV_ENTRY_METHOD_WHEELS;
    const bool recovered =
        fv_boot_recover_entry_method(&services, &method) == FV_BOOT_RECOVERY_OK;
    fv_app_t app;
    fv_app_init(&app, true, 0u, method);
    (void)fv_app_handle(&app, recovered ? FV_EVENT_BOOT_COMPLETED
                                       : FV_EVENT_FATAL_ERROR);
    return app.state;
}

static void test_invalid_metadata_cannot_reach_entry(void) {
    fake_context_t context = {
        .header = valid_header(FV_SECRET_METHOD_WHEELS_V1),
        .result = FV_PERSIST_OK,
    };

    context.header.entry_method = (fv_secret_method_t)99;
    CHECK(boot_with(&context) == FV_STATE_FAULT);
    context.header = valid_header(FV_SECRET_METHOD_WHEELS_V1);
    context.header.wrapped_vmk_length = 1u;
    CHECK(boot_with(&context) == FV_STATE_FAULT);
    context.header = valid_header(FV_SECRET_METHOD_WHEELS_V1);
    context.header.vault_id[0] = 0u;
    memset(context.header.vault_id, 0, sizeof(context.header.vault_id));
    CHECK(boot_with(&context) == FV_STATE_FAULT);
    context.result = FV_PERSIST_NOT_FOUND;
    CHECK(boot_with(&context) == FV_STATE_FAULT);
    context.result = FV_PERSIST_INVALID;
    CHECK(boot_with(&context) == FV_STATE_FAULT);
}

int main(void) {
    test_all_methods_survive_restart();
    test_invalid_metadata_cannot_reach_entry();
    puts("All boot-recovery tests passed.");
    return EXIT_SUCCESS;
}
