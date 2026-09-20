#ifndef FV_USB_STORAGE_H
#define FV_USB_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#ifndef FV_USB_IO_BYTES
#define FV_USB_IO_BYTES 32768u
#endif
/* Only the worker owns the vault. One worker batch overlaps the USB buffer. */
typedef enum {FV_USB_NONE, FV_USB_STATUS, FV_USB_READ, FV_USB_WRITE, FV_USB_LOCK, FV_USB_SYNC} fv_usb_op;
typedef struct {fv_usb_op op; uint32_t lba,count; uint8_t *data;} fv_usb_request;
typedef struct {int result; uint32_t blocks,generation; bool unlocked;} fv_usb_response;
fv_usb_response fv_usb_rpc(fv_usb_request request);
bool fv_usb_async_submit(fv_usb_request request);
bool fv_usb_async_take(fv_usb_response *response);
void fv_usb_async_wait(void);
/* Called by the local TinyUSB command-boundary extension before READ/WRITE10. */
bool fv_usb_storage_begin(uint8_t lun,bool write,uint32_t lba,uint32_t bytes,uint16_t block_size);
void fv_usb_storage_execute(const fv_usb_request *,fv_usb_response *);
void fv_usb_storage_poll(void);
void fv_usb_storage_clear_transport(void);
/* Core 0 only: successful MSC payload bytes since the previous sample. */
void fv_usb_storage_take_activity(uint32_t *read_bytes,uint32_t *write_bytes);
#endif
