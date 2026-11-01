#ifndef FV_USB_VAULT_IO_H
#define FV_USB_VAULT_IO_H
#include "usb_storage.h"
#include "fuse_vault/vault.h"

/* Complete the whole USB batch before acknowledging the request. */
static inline fv_block_result_t fv_usb_vault_io(fv_vault *vault,const fv_usb_request *request,
                                               fv_access_mode mode,uint32_t generation) {
    if(!request->data || !request->count || request->count>FV_USB_IO_BYTES/512 ||
       (request->op!=FV_USB_READ && request->op!=FV_USB_WRITE))return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if(!vault->unlocked || !fv_access_files(mode) || request->generation!=generation)
        return FV_BLOCK_ERROR_NOT_READY;
    if(request->op==FV_USB_WRITE && !fv_access_writable(mode))return FV_BLOCK_ERROR_READ_ONLY;
    if(request->lba>=vault->config.volume.logical_blocks ||
       request->count>vault->config.volume.logical_blocks-request->lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    return request->op==FV_USB_READ?
        fv_vault_read(vault,request->lba,request->count,request->data):
        fv_vault_write(vault,request->lba,request->count,request->data);
}
#endif
