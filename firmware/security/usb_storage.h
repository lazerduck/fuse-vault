#ifndef FV_USB_STORAGE_H
#define FV_USB_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#define FV_USB_IO_BYTES 4096u
/* Only the worker owns the vault. Core 0 passes one synchronous I/O at a time. */
typedef enum {FV_USB_NONE, FV_USB_STATUS, FV_USB_READ, FV_USB_WRITE, FV_USB_LOCK, FV_USB_SYNC} fv_usb_op;
typedef struct {fv_usb_op op; uint32_t lba,count; uint8_t *data;} fv_usb_request;
typedef struct {int result; uint32_t blocks,generation; bool unlocked;} fv_usb_response;
fv_usb_response fv_usb_rpc(fv_usb_request request);
void fv_usb_storage_execute(const fv_usb_request *,fv_usb_response *);
void fv_usb_storage_poll(void);
void fv_usb_storage_clear_transport(void);
#endif
