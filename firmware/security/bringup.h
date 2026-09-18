#ifndef FV_SECURITY_BRINGUP_H
#define FV_SECURITY_BRINGUP_H
#include "pico/util/queue.h"
#define FV_COMMAND_BYTES 96u
#define FV_REPLY_BYTES 24576u
/* Public diagnostic/test commands only; never real credentials or keys. */
typedef struct {char text[FV_COMMAND_BYTES];} fv_command;
extern queue_t commands,responses;
extern char reply[FV_REPLY_BYTES];
void security_worker(void);
void security_usb_poll(void);
void security_execute(const char *);
#endif
