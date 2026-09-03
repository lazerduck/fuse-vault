#include "fuse_vault/app.h"

#include "pico/stdlib.h"

static fv_app_t app;

static void execute_commands(fv_command_set_t commands) {
    /*
     * Platform implementations will be added as hardware drivers arrive.
     * Until then, every security-sensitive command is intentionally a no-op:
     * in particular, neither MSC nor FIDO USB interfaces can be attached.
     */
    (void)commands;
}

int main(void) {
    /* Provisioning state will ultimately be read from authenticated storage. */
    fv_app_init(&app, false, 0u);
    execute_commands(fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED));

    for (;;) {
        /* Input, UI rendering, and platform command dispatch will run here. */
        tight_loop_contents();
    }
}
