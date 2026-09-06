#define _GNU_SOURCE
#include "host_services.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/fido_store.h"
#include "fuse_vault/fido_probe.h"
#include "fuse_vault/fido_verification.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static fv_platform_services_t services;
static fv_host_services_context_t services_context;
static fv_app_t app;
static fv_device_runtime_t runtime;
static fv_fido_store_t store;
static fv_fido_probe_t transport;
static fv_fido_verification_t verification;
static uint32_t now = 100;
static int presence_result;
static bool attached, fail_commit;
static bool commit(void *context, const uint8_t *image, size_t size) {
    return !fail_commit && fv_fido_store_commit(context, image, size);
}
static void preview(const char *name) {
    const char *directory = getenv("FV_PASSKEY_PREVIEW_DIR");
    if (!directory) return;
    char path[4096]; snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb"); assert(file);
    fv_framebuffer_t frame; fv_ui_draw(&app, &frame);
    fprintf(file, "P6\n160 80\n255\n");
    for (unsigned y=0; y<80; ++y) for (unsigned x=0; x<160; ++x) {
        uint16_t v = frame.pixels[y][x];
        uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31)*255/31),
            (uint8_t)(((v >> 5) & 63)*255/63), (uint8_t)((v & 31)*255/31)};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}
static bool random_fill(void *context, uint8_t *out, size_t size) {
    (void)context; return services.ops->random_fill(&services, out, size);
}
static int presence(void *context) { (void)context; return presence_result; }
static uint32_t millis(void *context) { (void)context; return now; }
static uint8_t uv_retries(void *context) {
    (void)context; return (uint8_t)(FV_MAX_UNLOCK_ATTEMPTS - app.failed_attempts);
}
static bool verify_user(void *context, const uint8_t *rp_hash) {
    (void)context;
    return runtime.authentication.vmk_valid && app.session_unlocked &&
        fv_fido_verification_use(&verification, now, rp_hash);
}
static size_t command(void *context, uint32_t channel, const uint8_t *req,
                       size_t size, uint8_t *out, size_t capacity) {
    (void)context;
    return fv_fido_engine_command_channel(channel, req, size, out, capacity);
}
static bool attach_msc(void *context, fv_block_device_t *blocks) {
    (void)context; (void)blocks; return false;
}
static bool detach(void *context) {
    (void)context;
    fv_fido_engine_close(); fv_fido_store_close(&store);
    fv_fido_probe_reset(&transport); fv_fido_verification_clear(&verification);
    attached = false; return true;
}
static bool local_authorized(void *context) {
    (void)context;
    return attached && runtime.authentication.vmk_valid && app.session_unlocked &&
        verification.valid && (uint32_t)(now - verification.verified_at) < FV_FIDO_UV_COMPLETE_MS;
}
static bool manage(void *context, fv_passkey_action_t action, uint16_t *index,
    uint16_t *count, fv_passkey_t *entry) {
    (void)context;
    return fv_fido_engine_manage(action, index, count, entry);
}
static bool attach_fido(void *context, const fv_volume_master_key_t *vmk,
    const fv_media_layout_t *layout, const fv_encryption_stack_descriptor_t *stack) {
    (void)context;
    if (!fv_fido_store_open(&store, &services, &services_context.vault_device,
        layout, vmk, stack)) return false;
    const uint8_t device_id[16] = {'h','o','s','t','-','t','e','s','t'};
    fv_fido_engine_ops_t ops = {.random = random_fill, .commit = commit,
        .presence = presence, .millis = millis, .local_authorized = local_authorized, .verify_user = verify_user, .uv_retries = uv_retries, .context = &store};
    if (!fv_fido_engine_open(store.image, store.root_key, device_id, &ops)) return false;
    fv_fido_probe_reset(&transport);
    transport.dispatch = command;
    uint8_t init[64] = {255,255,255,255,0x86,0,8,1,2,3,4,5,6,7,8}, response[64];
    fv_fido_probe_receive(&transport, init, now);
    assert(fv_fido_probe_peek(&transport, response));
    assert(response[18] == 1); /* channel 1, network byte order at offsets 15..18 */
    fv_fido_probe_sent(&transport);
    attached = true;
    return true;
}
static const fv_runtime_usb_ops_t usb_ops = {attach_msc, detach, attach_fido, manage};
static const fv_credential_costs_t costs = {1,1};
static uint8_t secret_last = 56;
static void secret(fv_secret_entry_t *entry) {
    fv_secret_entry_begin(entry, FV_ENTRY_METHOD_WHEELS);
    entry->state.wheels.values[0] = 12;
    entry->state.wheels.values[1] = 34;
    entry->state.wheels.values[2] = secret_last;
}
static void unlock(void) {
    fail_commit = false;
    fv_device_runtime_handle_event(&runtime, FV_EVENT_LOCK_REQUESTED);
    if (app.state == FV_STATE_FAULT) {
        app.state = FV_STATE_VAULT_SECRET_ENTRY;
        /* Fault closed the engine; reopen recovers the durable snapshot. */
    }
    assert(app.state == FV_STATE_VAULT_SECRET_ENTRY);
    secret(&app.secret_entry);
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    assert(app.state == FV_STATE_MODE_SELECT && app.session_unlocked);
    fv_fido_verification_begin(&verification, now);
    app.selected_mode = FV_MODE_FIDO;
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    assert(app.state == FV_STATE_FIDO_READY && attached);
}
static size_t exchange(const uint8_t *req, size_t size, uint8_t *out) {
    if (!attached) { out[0] = 0x7f; return 1; }
    assert(size <= FV_FIDO_ENGINE_MESSAGE_SIZE);
    uint8_t report[64] = {0,0,0,1,0x90,(uint8_t)(size >> 8),(uint8_t)size};
    size_t used = size < 57 ? size : 57;
    memcpy(report + 7, req, used);
    fv_fido_probe_receive(&transport, report, now);
    uint8_t sequence = 0;
    while (used < size) {
        memset(report + 4, 0, 60); report[4] = sequence++;
        size_t count = size - used < 59 ? size - used : 59;
        memcpy(report + 5, req + used, count); used += count;
        fv_fido_probe_receive(&transport, report, now);
    }
    assert(fv_fido_probe_peek(&transport, report) && report[4] == 0x90);
    size_t response_size = ((size_t)report[5] << 8) | report[6];
    used = 0;
    while (fv_fido_probe_peek(&transport, report)) {
        size_t offset = used ? 5 : 7;
        size_t count = response_size - used < 64 - offset ? response_size - used : 64 - offset;
        memcpy(out + used, report + offset, count); used += count;
        fv_fido_probe_sent(&transport);
    }
    assert(used == response_size);
    return used;
}
int main(void) {
    char directory[] = "/tmp/fuse-vault-fido-device-XXXXXX";
    assert(mkdtemp(directory));
    assert(fv_host_services_init_sized(&services, &services_context, directory,
        4096u, FV_MEDIA_DEFAULT_FIDO_BLOCKS));
    fv_app_init(&app, false, 0, FV_ENTRY_METHOD_WHEELS);
    fv_app_set_fido_available(&app, true);
    assert(fv_device_runtime_init(&runtime, &app, &services,
        &services_context.vault_device, &usb_ops, NULL, &costs));
    fv_device_runtime_handle_event(&runtime, FV_EVENT_BOOT_COMPLETED);
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
    app.state = FV_STATE_PROVISIONING;
    secret(&app.setup_secret_entry);
    fv_device_runtime_execute(&runtime, FV_COMMAND_BEGIN_PROVISIONING);
    assert(app.provisioned);
    unlock();
    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        if (!strcmp(line,"reopen\n")) { now += 1; unlock(); puts("ok"); }
        else if (!strcmp(line,"change-password\n")) {
            fv_device_runtime_handle_event(&runtime, FV_EVENT_LOCK_REQUESTED);
            secret(&app.secret_entry);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            assert(app.state == FV_STATE_MODE_SELECT);
            while (app.selected_mode != FV_MODE_SETTINGS)
                fv_device_runtime_handle_event(&runtime, FV_EVENT_DOWN);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            secret(&app.secret_entry);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            assert(app.state == FV_STATE_SETTINGS && !attached);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            ++secret_last;
            secret(&app.secret_entry);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            secret(&app.secret_entry);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            assert(app.state == FV_STATE_CHANGE_REVIEW);
            fv_device_runtime_handle_event(&runtime, FV_EVENT_SELECT);
            assert(app.state == FV_STATE_CHANGE_SAVED && !attached);
            unlock();
            puts("ok");
        }
        else if (!strcmp(line,"deny\n")) { presence_result = 2; puts("ok"); }
        else if (!strcmp(line,"allow\n")) { presence_result = 0; puts("ok"); }
        else if (!strcmp(line,"lock\n")) { fv_device_runtime_handle_event(&runtime,FV_EVENT_LOCK_REQUESTED); puts("ok"); }
        else if (!strcmp(line,"expire\n")) { now += FV_FIDO_UV_COMPLETE_MS; puts("ok"); }
        else if (!strncmp(line,"ui-",3)) {
            fv_event_t event = FV_EVENT_SELECT;
            if (!strcmp(line,"ui-back\n")) event = FV_EVENT_BACK;
            else if (!strcmp(line,"ui-right\n")) event = FV_EVENT_RIGHT;
            else if (!strcmp(line,"ui-left\n")) event = FV_EVENT_LEFT;
            else if (!strcmp(line,"ui-up\n")) event = FV_EVENT_UP;
            else if (!strcmp(line,"ui-down\n")) event = FV_EVENT_DOWN;
            fv_device_runtime_handle_event(&runtime,event);
            if (app.state == FV_STATE_PASSKEY_DELETE_CONFIRM) preview("confirm");
            puts("ok");
        }
        else if (!strcmp(line,"check-list\n")) {
            assert(app.state == FV_STATE_PASSKEY_LIST && app.passkey_count == 1);
            assert(!strcmp(app.passkey.site,"example.com"));
            assert(!strcmp(app.passkey.account,"alice@example.com"));
            preview("list");
            puts("ok");
        }
        else if (!strcmp(line,"check-empty\n")) {
            assert(app.state == FV_STATE_PASSKEY_LIST && app.passkey_count == 0);
            preview("empty");
            puts("ok");
        }
        else if (!strcmp(line,"fail-commit\n")) { fail_commit = true; puts("ok"); }
        else if (!strcmp(line,"check-fault\n")) {
            assert(app.state == FV_STATE_FAULT && !app.session_unlocked && !attached);
            puts("ok");
        }
        else if (!strcmp(line,"check-two\n")) {
            assert(app.state == FV_STATE_PASSKEY_LIST && app.passkey_count == 2);
            assert(!strcmp(app.passkey.site, "example.com"));
            assert(app.passkey.account[0]);
            puts("ok");
        }
        else if (!strcmp(line,"check-bob\n")) {
            assert(!strcmp(app.passkey.account,"bob@example.com")); puts("ok");
        }
        else if (!strcmp(line,"check-cleared\n")) {
            fv_passkey_t empty = {0};
            assert(!memcmp(&app.passkey, &empty, sizeof(empty)));
            puts("ok");
        }
        else {
            size_t size = strcspn(line,"\n");
            assert(size % 2 == 0 && size / 2 <= FV_FIDO_ENGINE_MESSAGE_SIZE);
            uint8_t request[FV_FIDO_ENGINE_MESSAGE_SIZE], response[FV_FIDO_MESSAGE_SIZE];
            for (size_t i = 0; i < size / 2; ++i) { unsigned n; assert(sscanf(line + 2*i,"%2x",&n)==1); request[i]=(uint8_t)n; }
            size_t count = exchange(request, size / 2, response);
            for (size_t i = 0; i < count; ++i) printf("%02x",response[i]);
            puts("");
        }
        fflush(stdout);
    }
    fv_device_runtime_shutdown(&runtime);
    const char *names[] = {"device-secret.0","device-secret.1","device-secret-active.0",
        "device-secret-active.1","device-secret-revoked.0","device-secret-revoked.1",
        "security-journal.bin","vault-media.bin"};
    char path[4096];
    for (size_t i=0; i<sizeof(names)/sizeof(names[0]); ++i) {
        snprintf(path,sizeof(path),"%s/%s",directory,names[i]); (void)unlink(path);
    }
    assert(rmdir(directory)==0);
    return 0;
}
