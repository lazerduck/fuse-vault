#ifndef FV_DEVICE_UI_H
#define FV_DEVICE_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "credential_entry.h"
#define FV_SCREEN_BYTES 1600
#define FV_UI_SECRET_MAX 64
/* Stable profile-2 mapping: up=1, down=2, left=3, right=4. Select submits. */
typedef enum {UI_UP=1,UI_DOWN,UI_LEFT,UI_RIGHT,UI_SELECT,UI_BACK} fv_ui_key;
typedef enum {UI_STATUS=1,UI_CREATE,UI_UNLOCK,UI_LOCK,UI_POLICY,UI_ERASE,UI_CHANGE} fv_ui_operation;
typedef struct {
    fv_ui_operation op;
    uint8_t secret[64],length,count;
    uint8_t current[64],current_length;
    uint16_t algorithms[4],profile;
    uint32_t attempts,action;
} fv_ui_job;
typedef struct {
    int result,status;
    uint16_t profile;
    bool unlocked;
    uint64_t blocks;
    uint32_t attempts,action;
} fv_ui_result;
typedef enum {UI_WAIT,UI_HOME,UI_SECRET,UI_CONFIRM,UI_STACK,UI_OPTIONS,UI_REVIEW,UI_ERASE_CONFIRM,UI_ERROR,UI_METHOD,UI_SETTINGS} fv_ui_screen;
typedef struct {
    fv_ui_screen screen;
    fv_ui_result device;
    fv_ui_job job;
    fv_credential_entry entry;
    bool pending,settings,flipped,changing;
    unsigned cursor;
    uint8_t confirmation[64],confirmation_length;
    int error;
    uint32_t format_done,format_total,format_milliseconds;
    uint8_t framebuffer[FV_SCREEN_BYTES];
} fv_ui;
void fv_ui_init(fv_ui *);
/* Inputs name physical buttons; orientation maps directions before navigation
 * or credential encoding. Select/Back keep their semantic roles. */
void fv_ui_keypress(fv_ui *,fv_ui_key);
void fv_ui_complete(fv_ui *,fv_ui_result);
void fv_ui_cancel(fv_ui *);
void fv_ui_render(fv_ui *);
/* Volatile wipe for credentials in UI objects and inter-core messages. */
void fv_ui_wipe(void *,size_t);
/* C wins if both supplies are present: 0=none, 1=C, 2=A. */
static inline unsigned fv_usb_route(bool a,bool c){return c?1:a?2:0;}
#endif
