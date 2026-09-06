#include "fuse_vault/app.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char key;
    fv_event_t event;
    const char *description;
} key_binding_t;

static const key_binding_t bindings[] = {
    {'u', FV_EVENT_UP, "D-pad up"},
    {'d', FV_EVENT_DOWN, "D-pad down"},
    {'l', FV_EVENT_LEFT, "D-pad left"},
    {'r', FV_EVENT_RIGHT, "D-pad right"},
    {'s', FV_EVENT_SELECT, "D-pad select"},
    {'b', FV_EVENT_BACK, "back"},
    {'y', FV_EVENT_AUTH_SUCCEEDED, "authentication succeeded"},
    {'x', FV_EVENT_AUTH_FAILED, "authentication failed"},
    {'c', FV_EVENT_ATTEMPT_COUNTER_STORED, "attempt counter stored"},
    {'o', FV_EVENT_PROVISIONING_SUCCEEDED, "provisioning succeeded"},
    {'m', FV_EVENT_MEDIA_FOUND, "SD media found"},
    {'p', FV_EVENT_MEDIA_PREPARED, "SD media prepared"},
    {'n', FV_EVENT_MEDIA_FAILED, "SD media failed"},
    {'e', FV_EVENT_USB_EJECTED, "USB ejected"},
    {'k', FV_EVENT_LOCK_REQUESTED, "lock"},
    {'f', FV_EVENT_STORAGE_FAILED, "storage failure"},
};

static void print_commands(fv_command_set_t commands) {
    static const struct {
        fv_command_t command;
        const char *name;
    } names[] = {
        {FV_COMMAND_BEGIN_PROVISIONING, "begin-provisioning"},
        {FV_COMMAND_BEGIN_AUTHENTICATION, "begin-authentication"},
        {FV_COMMAND_USB_ATTACH_MSC, "usb-attach-msc"},
        {FV_COMMAND_USB_ATTACH_FIDO, "usb-attach-fido"},
        {FV_COMMAND_USB_DETACH, "usb-detach"},
        {FV_COMMAND_ERASE_TRANSIENT_SECRET, "erase-transient-secret"},
        {FV_COMMAND_ERASE_SESSION_KEYS, "erase-session-keys"},
        {FV_COMMAND_DESTROY_DEVICE_SECRET, "destroy-device-secret"},
        {FV_COMMAND_STORE_ATTEMPT_COUNTER, "store-attempt-counter"},
        {FV_COMMAND_INSPECT_MEDIA, "inspect-media"},
        {FV_COMMAND_PREPARE_MEDIA, "prepare-media"},
    };

    if (commands == FV_COMMAND_NONE) {
        return;
    }

    printf("commands:");
    for (size_t index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        if ((commands & (fv_command_set_t)names[index].command) != 0u) {
            printf(" %s", names[index].name);
        }
    }
    putchar('\n');
}

static void draw(const fv_app_t *app, fv_command_set_t commands) {
    fv_ui_view_t view;
    fv_app_render(app, &view);

    printf("\n+--------------------------------+\n");
    printf("| %-30s |\n", view.title);
    printf("+--------------------------------+\n");
    for (size_t index = 0u; index < FV_UI_LINE_COUNT; ++index) {
        printf("| %-30s |\n", view.lines[index]);
    }
    printf("+--------------------------------+\n");
    printf("state: %s\n", fv_state_name(app->state));
    print_commands(commands);
}

static bool lookup_event(char key, fv_event_t *event) {
    for (size_t index = 0u; index < sizeof(bindings) / sizeof(bindings[0]); ++index) {
        if (bindings[index].key == key) {
            *event = bindings[index].event;
            return true;
        }
    }
    return false;
}

static void print_help(void) {
    puts("Controls:");
    for (size_t index = 0u; index < sizeof(bindings) / sizeof(bindings[0]); ++index) {
        printf("  %c  %s\n", bindings[index].key, bindings[index].description);
    }
    puts("  q  quit");
}

int main(int argc, char **argv) {
    const bool provisioned = !(argc == 2 && strcmp(argv[1], "--unprovisioned") == 0);
    fv_app_t app;
    fv_app_init(&app, provisioned, 0u, FV_ENTRY_METHOD_WHEELS);
    fv_command_set_t commands = fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);

    puts("Fuse Vault host simulator");
    puts("Authentication outcomes are injected because no secret backend exists yet.");
    print_help();
    draw(&app, commands);

    for (;;) {
        int input;
        printf("\n> ");
        fflush(stdout);
        input = getchar();
        if (input == EOF || input == 'q') {
            break;
        }

        int discard;
        while ((discard = getchar()) != '\n' && discard != EOF) {
        }

        fv_event_t event;
        if (!lookup_event((char)input, &event)) {
            puts("Unknown input. Press one of the listed keys.");
            continue;
        }

        commands = fv_app_handle(&app, event);
        draw(&app, commands);
    }

    return 0;
}
