#ifndef FUSE_VAULT_APP_H
#define FUSE_VAULT_APP_H

#include <stdbool.h>
#include <stdint.h>

#define FV_MAX_UNLOCK_ATTEMPTS 10u
#define FV_UI_LINE_COUNT 4u
#define FV_UI_TEXT_CAPACITY 32u
#define FV_SECRET_WHEEL_COUNT 3u
#define FV_SECRET_WHEEL_VALUES 100u

typedef enum {
    FV_STATE_BOOTING = 0,
    FV_STATE_SETUP_REQUIRED,
    FV_STATE_SETUP_METHOD_SELECT,
    FV_STATE_SETUP_SECRET_ENTRY,
    FV_STATE_SETUP_SECRET_CONFIRM,
    FV_STATE_SETUP_SECRET_MISMATCH,
    FV_STATE_SETUP_POLICY_CONFIRM,
    FV_STATE_PROVISIONING,
    FV_STATE_MODE_SELECT,
    FV_STATE_VAULT_SECRET_ENTRY,
    FV_STATE_VAULT_AUTHENTICATING,
    FV_STATE_VAULT_RECORDING_SUCCESS,
    FV_STATE_VAULT_RECORDING_FAILURE,
    FV_STATE_VAULT_UNLOCKED,
    FV_STATE_FIDO_READY,
    FV_STATE_DESTROYED,
    FV_STATE_FAULT,
} fv_state_t;

typedef enum {
    FV_MODE_VAULT = 0,
    FV_MODE_FIDO,
    FV_MODE_COUNT,
} fv_mode_t;

typedef enum {
    FV_ENTRY_METHOD_WHEELS = 0,
    FV_ENTRY_METHOD_COUNT,
} fv_entry_method_t;

typedef enum {
    FV_EVENT_BOOT_COMPLETED = 0,
    FV_EVENT_UP,
    FV_EVENT_DOWN,
    FV_EVENT_LEFT,
    FV_EVENT_RIGHT,
    FV_EVENT_SELECT,
    FV_EVENT_BACK,
    FV_EVENT_AUTH_SUCCEEDED,
    FV_EVENT_AUTH_FAILED,
    FV_EVENT_ATTEMPT_COUNTER_STORED,
    FV_EVENT_PROVISIONING_SUCCEEDED,
    FV_EVENT_PROVISIONING_FAILED,
    FV_EVENT_LOCK_REQUESTED,
    FV_EVENT_USB_EJECTED,
    FV_EVENT_STORAGE_FAILED,
    FV_EVENT_FATAL_ERROR,
} fv_event_t;

typedef enum {
    FV_COMMAND_NONE = 0,
    FV_COMMAND_BEGIN_PROVISIONING = 1u << 0,
    FV_COMMAND_BEGIN_AUTHENTICATION = 1u << 1,
    FV_COMMAND_USB_ATTACH_MSC = 1u << 2,
    FV_COMMAND_USB_ATTACH_FIDO = 1u << 3,
    FV_COMMAND_USB_DETACH = 1u << 4,
    FV_COMMAND_ERASE_TRANSIENT_SECRET = 1u << 5,
    FV_COMMAND_ERASE_SESSION_KEYS = 1u << 6,
    FV_COMMAND_DESTROY_DEVICE_SECRET = 1u << 7,
    FV_COMMAND_STORE_ATTEMPT_COUNTER = 1u << 8,
} fv_command_t;

typedef uint32_t fv_command_set_t;

typedef struct {
    fv_state_t state;
    fv_mode_t selected_mode;
    fv_entry_method_t selected_entry_method;
    uint8_t secret_wheels[FV_SECRET_WHEEL_COUNT];
    uint8_t setup_secret_wheels[FV_SECRET_WHEEL_COUNT];
    uint8_t selected_secret_wheel;
    uint8_t failed_attempts;
    bool provisioned;
} fv_app_t;

typedef struct {
    char title[FV_UI_TEXT_CAPACITY];
    char lines[FV_UI_LINE_COUNT][FV_UI_TEXT_CAPACITY];
} fv_ui_view_t;

void fv_app_init(fv_app_t *app, bool provisioned, uint8_t persisted_failed_attempts);
fv_command_set_t fv_app_handle(fv_app_t *app, fv_event_t event);
void fv_app_render(const fv_app_t *app, fv_ui_view_t *view);
const char *fv_state_name(fv_state_t state);

#endif
