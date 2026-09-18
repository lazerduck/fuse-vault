/* Exercise the actual simulator orchestration without requiring a display. */
#define main simulator_window_main
#include "display_simulator.c"
#undef main

#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "simulator test line %d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

static void press(simulator_t *s, fv_input_id_t input) {
    static uint32_t now;
    if (now == 0u || (int32_t)(monotonic_ms() - now) > 0) now = monotonic_ms();
    fv_input_controller_update(&s->input_controller, FV_INPUT_BIT(input),
                               now, emit_input_event, s);
    fv_input_controller_update(&s->input_controller, FV_INPUT_BIT(input),
                               now + 30u, emit_input_event, s);
    fv_input_controller_update(&s->input_controller, 0u,
                               now + 31u, emit_input_event, s);
    fv_input_controller_update(&s->input_controller, 0u,
                               now + 61u, emit_input_event, s);
    now += 62u;
}

#ifdef FUSE_VAULT_SIMULATOR_FIDO2
static bool cancel_test, lock_test;
static gboolean approve_test(gpointer data) {
    simulator_t *s = data;
    if (s->app.fido_waiting) {
        if (lock_test) dispatch_event(s, FV_EVENT_LOCK_REQUESTED);
        else press(s, cancel_test ? FV_INPUT_BACK : FV_INPUT_SELECT);
    }
    return G_SOURCE_CONTINUE;
}
static void host_test(simulator_t *s, const char *op, bool success) {
    guint timer = g_timeout_add(5u, approve_test, s);
    run_fido_host(s, op, "example.com", "alice@example.com");
    g_source_remove(timer);
    fprintf(stderr, "%s: %s\n", op, s->host_result);
    CHECK(s->host_success == success);
}
#endif
int main(void) {
    char *directory = g_dir_make_tmp("fuse-vault-gui-test-XXXXXX", NULL);
    CHECK(directory != NULL);
    simulator_t s = {.lock_fd = -1};
    CHECK(select_directory(&s, directory));
    CHECK(s.app.state == FV_STATE_SETUP_REQUIRED);
    simulator_t competing = {.lock_fd = -1};
    CHECK(!select_directory(&competing, directory));
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_MEDIA_CONFIRM);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_METHOD_SELECT);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_SECRET_ENTRY);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_SECRET_CONFIRM);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_STACK_SELECT);
    press(&s, FV_INPUT_DOWN);
    press(&s, FV_INPUT_DOWN);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETUP_POLICY_CONFIRM);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_VAULT_SECRET_ENTRY);
    CHECK(boot(&s));
    press(&s, FV_INPUT_SELECT); /* wrong password 00/00/00 */
    CHECK(s.app.failed_attempts == 1u && !s.msc.attached);
    CHECK(boot(&s));
    CHECK(s.app.failed_attempts == 1u);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_MODE_SELECT && !s.msc.attached);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_VAULT_UNLOCKED && s.msc.attached);
    uint8_t note[FV_BLOCK_SIZE] = "Persistent ergonomic simulator note";
    uint8_t recovered[FV_BLOCK_SIZE] = {0};
    CHECK(fv_virtual_msc_write10(&s.msc, 0u, 1u, note) == FV_MSC_OK);
    CHECK(fv_virtual_msc_synchronize_cache(&s.msc) == FV_MSC_OK);
    CHECK(boot(&s));
    CHECK(fv_virtual_msc_read10(&s.msc, 0u, 1u, recovered) == FV_MSC_NOT_READY);
    CHECK(!s.runtime.authentication.vmk_valid);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_MODE_SELECT && !s.msc.attached);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.msc.attached);
    CHECK(fv_virtual_msc_read10(&s.msc, 0u, 1u, recovered) == FV_MSC_OK);
    CHECK(memcmp(note, recovered, sizeof(note)) == 0);
    dispatch_event(&s, FV_EVENT_USB_EJECTED);
    CHECK(!s.msc.attached && !s.runtime.authentication.vmk_valid);
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    press(&s, FV_INPUT_UP); press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_DOWN); press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_FIDO_READY && s.fido.attached && !s.msc.attached);
    cancel_test = true;
    host_test(&s, "register", false);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_PASSKEY_LIST && s.app.passkey_count == 0);
    press(&s, FV_INPUT_BACK);
    cancel_test = false;
    host_test(&s, "register", true);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_PASSKEY_LIST && s.app.passkey_count == 1);
    CHECK(strcmp(s.app.passkey.site, "example.com") == 0);
    dispatch_event(&s, FV_EVENT_LOCK_REQUESTED);
    CHECK(!s.fido.attached && !s.fido.verification.valid);
#endif
    /* Real button mappings: change method, confirm independently, reboot and
     * read the same encrypted note. No UI state is injected for this flow. */
    press(&s, FV_INPUT_UP); press(&s, FV_INPUT_SELECT); /* current wheels */
    CHECK(s.app.state == FV_STATE_MODE_SELECT && !s.msc.attached);
    press(&s, FV_INPUT_DOWN); /* settings (after FIDO when enabled) */
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    press(&s, FV_INPUT_DOWN);
#endif
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_SETTINGS && !s.msc.attached);
    press(&s, FV_INPUT_DOWN); press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_DOWN); press(&s, FV_INPUT_SELECT); /* directions */
    for (unsigned i = 0; i < 6; ++i) press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_CHANGE_CONFIRM);
    for (unsigned i = 0; i < 6; ++i) press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_CHANGE_REVIEW);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_CHANGE_SAVED && !s.runtime.authentication.vmk_valid);
    CHECK(boot(&s));
    CHECK(s.app.selected_entry_method == FV_ENTRY_METHOD_DIRECTIONS);
    for (unsigned i = 0; i < 6; ++i) press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_MODE_SELECT && !s.msc.attached);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.msc.attached);
    CHECK(fv_virtual_msc_read10(&s.msc, 0u, 1u, recovered) == FV_MSC_OK);
    CHECK(memcmp(note, recovered, sizeof(note)) == 0);
#ifdef FUSE_VAULT_SIMULATOR_FIDO2
    CHECK(boot(&s));
    for (unsigned i = 0; i < 6; ++i) press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_DOWN); press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_FIDO_READY);
    host_test(&s, "authenticate", true);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.passkey_count == 1);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_PASSKEY_DELETE_CONFIRM);
    press(&s, FV_INPUT_BACK); /* cancellation preserves key */
    CHECK(s.app.passkey_count == 1);
    press(&s, FV_INPUT_SELECT); press(&s, FV_INPUT_RIGHT);
    CHECK(s.app.state == FV_STATE_PASSKEY_LIST && s.app.passkey_count == 0);
    press(&s, FV_INPUT_BACK);
    host_test(&s, "authenticate", false);
    lock_test = true;
    host_test(&s, "register", false);
    CHECK(!s.fido.attached && !s.runtime.authentication.vmk_valid);
    CHECK(s.app.state == FV_STATE_VAULT_SECRET_ENTRY);
#endif
    fv_device_runtime_shutdown(&s.runtime);
    close(s.lock_fd);
    GDir *dir = g_dir_open(directory, 0u, NULL);
    CHECK(dir != NULL);
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        char *path = g_build_filename(directory, name, NULL);
        CHECK(unlink(path) == 0);
        g_free(path);
    }
    g_dir_close(dir);
    CHECK(rmdir(directory) == 0);
    g_free(s.directory);
    g_free(directory);
    /* The previous small-media format remains readable, without resizing. */
    char *legacy_dir = g_dir_make_tmp("fuse-vault-legacy-gui-XXXXXX", NULL);
    CHECK(legacy_dir);
    fv_platform_services_t legacy_services;
    fv_host_services_context_t legacy_context;
    CHECK(fv_host_services_init(&legacy_services, &legacy_context, legacy_dir));
    simulator_t legacy = {.lock_fd = -1};
    CHECK(select_directory(&legacy, legacy_dir));
    CHECK(!legacy.app.fido_available);
    CHECK(legacy.services_context.vault_device.ops->block_count(
        &legacy.services_context.vault_device) == 256u);
    fv_device_runtime_shutdown(&legacy.runtime);
    close(legacy.lock_fd);
    dir = g_dir_open(legacy_dir, 0u, NULL);
    CHECK(dir);
    while ((name = g_dir_read_name(dir)) != NULL) {
        char *path = g_build_filename(legacy_dir, name, NULL);
        CHECK(unlink(path) == 0); g_free(path);
    }
    g_dir_close(dir);
    CHECK(rmdir(legacy_dir) == 0);
    g_free(legacy.directory); g_free(legacy_dir);
    puts("Simulator input, persistence, storage and enabled FIDO flows passed.");
    return 0;
}
