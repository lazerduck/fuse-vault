#if !defined(FV_DEBUG_OTP_INSPECT) || !FV_DEBUG_OTP_INSPECT
#error OTP inspection must not be compiled into a non-inspection build
#endif
#include "fuse_vault/rp2354_otp_inspect.h"
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"
#include "hardware/regs/otp_data.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static int read_raw(uint32_t row,uint32_t *out) {
    *out=0;
    /* No caller-supplied command flags: WRITE and ECC are always absent. */
    return rom_func_otp_access((uint8_t *)out,4,(otp_cmd_t){.flags=row});
}
int fv_rp2354_otp_inspect(unsigned page,fv_otp_page_summary *out) {
    if(!out)return -1;
    memset(out,0,sizeof(*out));if(page>=64)return -1;
    for(unsigned i=0;i<2;i++) {
        out->lock_status[i]=read_raw(OTP_DATA_PAGE0_LOCK0_ROW+page*2+i,&out->lock_raw[i]);
        if(out->lock_status[i])out->lock_raw[i]=0;
    }
    out->software_lock=otp_hw->sw_lock[page];
    for(unsigned i=0;i<64;i++) {
        uint32_t value;int status=read_raw(page*64+i,&value);
        if(status) {
            if(!out->first_read_error)out->first_read_error=status;
            out->unreadable++;
        } else {
            value&=0xffffff;
            if(!value)out->blank++;
            else if(value==0xffffff)out->all_ones++;
            else out->programmed++;
        }
        mbedtls_platform_zeroize(&value,sizeof(value));
    }
    return 0;
}
