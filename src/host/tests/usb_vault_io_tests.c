#include "usb_vault_io.h"
#include <assert.h>
#include <string.h>
static unsigned calls,fail_at;
static uint64_t next_lba;
static uint8_t *next_data;
static bool writing;
static fv_block_result_t io(uint64_t lba,uint32_t count,uint8_t *data,bool write) {
    assert(write==writing && lba==next_lba && data==next_data);
    assert(count==127);
    next_lba+=count;next_data+=count*512;++calls;
    return calls==fail_at?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;
}
fv_block_result_t fv_vault_read(fv_vault *v,uint64_t lba,uint32_t count,uint8_t *data){(void)v;return io(lba,count,data,false);}
fv_block_result_t fv_vault_write(fv_vault *v,uint64_t lba,uint32_t count,uint8_t *data){(void)v;return io(lba,count,data,true);}
int main(void) {
    static _Alignas(4) uint8_t data[FV_USB_IO_BYTES];
    fv_vault vault={0};vault.unlocked=true;vault.config.volume.logical_blocks=1000;
    for(unsigned write=0;write<2;write++)for(unsigned fail=0;fail<2;fail++) {
        fv_usb_request request={.op=write?FV_USB_WRITE:FV_USB_READ,.lba=20,.count=127,.data=data};
        calls=0;fail_at=fail;next_lba=20;next_data=data;writing=write;
        assert(fv_usb_vault_io(&vault,&request)==(fail?FV_BLOCK_ERROR_IO:FV_BLOCK_OK));
        assert(calls==1);
    }
    fv_usb_request request={.op=FV_USB_WRITE,.lba=900,.count=127,.data=data};calls=0;
    assert(fv_usb_vault_io(&vault,&request)==FV_BLOCK_ERROR_OUT_OF_RANGE && !calls);
    request.lba=0;request.count=128;
    assert(fv_usb_vault_io(&vault,&request)==FV_BLOCK_ERROR_INVALID_ARGUMENT && !calls);
    request.count=127;vault.unlocked=false;
    assert(fv_usb_vault_io(&vault,&request)==FV_BLOCK_ERROR_NOT_READY && !calls);
    return 0;
}
