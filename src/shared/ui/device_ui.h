#ifndef FV_DEVICE_UI_H
#define FV_DEVICE_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "credential_entry.h"
/* Row-major RGB332: RRR GGG BB, one byte per pixel. */
#define FV_SCREEN_WIDTH 160u
#define FV_SCREEN_HEIGHT 80u
#define FV_SCREEN_BYTES (FV_SCREEN_WIDTH * FV_SCREEN_HEIGHT)
typedef uint8_t fv_ui_colour;
#define UI_COLOUR_BACKGROUND 0x00u
#define UI_COLOUR_TEXT 0xffu
#define UI_COLOUR_ACCENT 0x1fu
#define UI_COLOUR_MUTED 0x92u
#define UI_COLOUR_SUCCESS 0x1cu
#define UI_COLOUR_WARNING 0xf0u
#define UI_COLOUR_ERROR 0xe0u
#define FV_UI_SECRET_MAX 64
#define FV_UI_HISTORY_SAMPLES 60u
/* Stable profile-2 mapping: up=1, down=2, left=3, right=4. Select submits. */
typedef enum { UI_UP = 1, UI_DOWN, UI_LEFT, UI_RIGHT, UI_SELECT, UI_BACK } fv_ui_key;
typedef enum {
    UI_STATUS = 1,
    UI_CREATE,
    UI_UNLOCK,
    UI_LOCK,
    UI_POLICY,
    UI_ERASE,
    UI_CHANGE,
    UI_FIDO_INIT,
    UI_FIDO_POLICY,
    UI_PASSKEY_LIST,
    UI_PASSKEY_DELETE
} fv_ui_operation;
typedef struct {
    fv_ui_operation op;
    uint8_t secret[FV_UI_SECRET_MAX], length, count;
    uint8_t current[FV_UI_SECRET_MAX], current_length;
    uint16_t algorithms[4], profile;
    uint32_t attempts, action;
    uint8_t fido_policy;
    uint16_t passkey_index;
    uint8_t passkey_id[42];
} fv_ui_job;
typedef struct {
    int result, status;
    uint16_t profile;
    bool unlocked;
    uint64_t blocks;
    uint32_t attempts, action;
    uint8_t fido_policy;
    uint16_t passkey_index, passkey_count;
    uint8_t passkey_id[42];
    char passkey_site[256], passkey_account[256];
} fv_ui_result;
typedef enum {
    UI_WAIT,
    UI_HOME,
    UI_SECRET,
    UI_CONFIRM,
    UI_STACK,
    UI_OPTIONS,
    UI_REVIEW,
    UI_ERASE_CONFIRM,
    UI_ERROR,
    UI_METHOD,
    UI_SETTINGS,
    UI_FIDO_APPROVE,
    UI_FIDO_INIT_CONFIRM,
    UI_FIDO_POLICY_SCREEN,
    UI_PASSKEYS,
    UI_PASSKEY_DELETE_CONFIRM,
    UI_DASHBOARD,
    UI_DASHBOARD_SETTING,
    UI_SCREEN_COUNT
} fv_ui_screen;
/* A credential screen can participate in exactly one vault workflow. FIDO's
 * modal handshake is separate and is owned by the device adapter. */
typedef enum { UI_FLOW_SETUP, UI_FLOW_UNLOCK, UI_FLOW_POLICY, UI_FLOW_CHANGE } fv_ui_flow;
typedef struct {
    fv_ui_screen screen;
    fv_ui_flow flow;
    /* Last worker response and the next request awaiting adapter pickup. */
    fv_ui_result device;
    fv_ui_job job;
    fv_credential_entry entry;
    bool pending, flipped;
    /* Modal approval handshake; generation invalidates held/stale input. */
    bool fido_enabled, fido_modal, fido_done, fido_approved;
    char fido_label[128];
    uint32_t fido_generation;
    /* Selection belongs to the active scene; passkey text paging survives
     * transitions between browsing and its delete confirmation. */
    unsigned cursor;
    unsigned passkey_page;
    uint8_t confirmation[FV_UI_SECRET_MAX], confirmation_length;
    int error;
    uint32_t read_kib_tenths, write_kib_tenths;
    bool read_active, write_active;
    /* Half-second samples; volatile dashboard choice survives lock/unlock. */
    uint32_t read_history[FV_UI_HISTORY_SAMPLES], write_history[FV_UI_HISTORY_SAMPLES];
    unsigned history_next, history_count;
    bool dashboard_bars;
    unsigned busy_frame;
    uint32_t format_done, format_total, format_milliseconds;
    uint8_t framebuffer[FV_SCREEN_BYTES];
} fv_ui;
void fv_ui_init(fv_ui *);
void fv_ui_fido_begin(fv_ui *, bool secret, uint16_t profile, const char *label);
void fv_ui_fido_end(fv_ui *);
/* Inputs name physical buttons; orientation maps directions before navigation
 * or credential encoding. Select/Back keep their semantic roles. */
void fv_ui_keypress(fv_ui *, fv_ui_key);
void fv_ui_complete(fv_ui *, fv_ui_result);
void fv_ui_cancel(fv_ui *);
void fv_ui_render(fv_ui *);
/* Call from the UI thread; redraw waiting scenes at most every 100 ms. */
void fv_ui_animate(fv_ui *, uint32_t milliseconds);
/* Host reads are downloads; host writes are uploads into the vault. */
void fv_ui_activity(fv_ui *, uint32_t read_bytes, uint32_t write_bytes, uint64_t elapsed_us);
/* Volatile wipe for credentials in UI objects and inter-core messages. */
void fv_ui_wipe(void *, size_t);
#endif
