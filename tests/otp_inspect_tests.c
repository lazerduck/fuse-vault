#include "fuse_vault/rp2354_otp_inspect.h"
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
otp_hw_t fake_otp;
static unsigned calls;
int rom_func_otp_access(uint8_t *buffer,uint32_t bytes,otp_cmd_t command) {
    CHECK(bytes==4 && !((uintptr_t)buffer&3));
    CHECK(command.flags<4096); /* In particular, no WRITE/ECC or caller flags. */
    calls++;uint32_t value=0,row=command.flags;
    if(row==0xf80+6)value=0x010101;
    if(row==0xf80+7)value=0x030303;
    if(row==192)value=0xffffff;
    if(row==193)value=0x123456;
    if(row==194){memset(buffer,0xa5,4);return -4;}
    if(row>=256 && row<320)return -4;
    memcpy(buffer,&value,4);return 0;
}
int main(void) {
    fake_otp.sw_lock[3]=7;fv_otp_page_summary s;
    CHECK(!fv_rp2354_otp_inspect(3,&s));CHECK(calls==66);
    CHECK(s.blank==61 && s.programmed==1 && s.all_ones==1 && s.unreadable==1 && s.first_read_error==-4);
    CHECK(s.lock_raw[0]==0x010101 && s.lock_raw[1]==0x030303 && s.software_lock==7);
    CHECK(!fv_rp2354_otp_inspect(4,&s));CHECK(s.unreadable==64 && s.blank==0 && s.first_read_error==-4);
    unsigned before=calls;CHECK(fv_rp2354_otp_inspect(64,&s));CHECK(calls==before);
    CHECK(fv_rp2354_otp_inspect(0,NULL));CHECK(calls==before);
    puts("OTP inspection: read-only flags, lock rows, aggregate occupancy, denied != blank, bounds passed");
    return 0;
}
