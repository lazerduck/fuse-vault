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
    CHECK(s.app.state == FV_STATE_MODE_SELECT);
    CHECK(boot(&s));
    press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_SELECT); /* wrong password 00/00/00 */
    CHECK(s.app.failed_attempts == 1u && !s.msc.attached);
    CHECK(boot(&s));
    CHECK(s.app.failed_attempts == 1u);
    press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.app.state == FV_STATE_VAULT_UNLOCKED && s.msc.attached);
    uint8_t note[FV_BLOCK_SIZE] = "Persistent ergonomic simulator note";
    uint8_t recovered[FV_BLOCK_SIZE] = {0};
    CHECK(fv_virtual_msc_write10(&s.msc, 0u, 1u, note) == FV_MSC_OK);
    CHECK(fv_virtual_msc_synchronize_cache(&s.msc) == FV_MSC_OK);
    CHECK(boot(&s));
    CHECK(fv_virtual_msc_read10(&s.msc, 0u, 1u, recovered) == FV_MSC_NOT_READY);
    CHECK(!s.runtime.authentication.vmk_valid);
    press(&s, FV_INPUT_SELECT);
    press(&s, FV_INPUT_UP);
    press(&s, FV_INPUT_SELECT);
    CHECK(s.msc.attached);
    CHECK(fv_virtual_msc_read10(&s.msc, 0u, 1u, recovered) == FV_MSC_OK);
    CHECK(memcmp(note, recovered, sizeof(note)) == 0);
    dispatch_event(&s, FV_EVENT_USB_EJECTED);
    CHECK(!s.msc.attached && !s.runtime.authentication.vmk_valid);
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
    puts("Simulator input/setup/restart/unlock/encrypted storage/eject passed.");
    return 0;
}
