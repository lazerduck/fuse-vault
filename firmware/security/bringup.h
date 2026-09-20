#ifndef FV_SECURITY_BRINGUP_H
#define FV_SECURITY_BRINGUP_H
#include "pico/util/queue.h"
#define FV_COMMAND_BYTES 96u
#define FV_REPLY_BYTES 24576u
/* Public diagnostic/test commands only; never real credentials or keys. */
#if FV_USB_MSC
#include "usb_storage.h"
#endif
#if FV_DEVICE_UI
#include "device_ui.h"
extern queue_t ui_responses;
#endif
typedef struct {
#if FV_DEVICE_UI
    fv_ui_job *ui; /* Single in-flight mailbox; queue never retains credential bytes. */
#endif
    char text[FV_COMMAND_BYTES];
#if FV_USB_MSC
    fv_usb_request storage;
#endif
} fv_command;
#if FV_USB_MSC
extern queue_t storage_responses;
#endif
extern queue_t commands,responses;
extern char reply[FV_REPLY_BYTES];
#if FV_DEBUG_STARTUP
extern volatile uint32_t startup_stage;
extern volatile bool startup_requested;
#endif
void security_worker(void);
void security_usb_poll(void);
void security_execute(const char *);
#if FV_DEBUG_BOOT_TRACE
void boot_trace_mark(uint32_t stage);
void boot_trace_poll(void);
#else
static inline void boot_trace_mark(uint32_t stage){(void)stage;}
static inline void boot_trace_poll(void){}
#endif
#endif
