#include "fuse_vault/rp2354_authority.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/unique_id.h"
#include "hardware/flash.h"
#include "hardware/structs/otp.h"
#include "hardware/regs/otp_data.h"
#include <mbedtls/platform_util.h>
#include <string.h>
_Static_assert(FLASH_PAGE_SIZE==256 && FLASH_SECTOR_SIZE==4096,"Review journal geometry");
_Static_assert(FV_SECURITY_FLASH_OFFSET+FV_JOURNAL_BYTES==2*1024*1024,"RP2354 2 MiB layout");
static bool app_page(unsigned page){return page>=FV_OTP_ROOT_PAGE && page<FV_OTP_TOKEN_PAGE+FV_OTP_TOKEN_SLOTS;}
static int raw_read(void *ctx,uint32_t row,uint32_t *out) {
    (void)ctx;*out=0;
    if(row>=4096)return -1;
    return rom_func_otp_access((uint8_t*)out,4,(otp_cmd_t){.flags=row});
}
static int programmable(void *ctx,unsigned page) {
    uint32_t l0,l1;
    if(!app_page(page) || raw_read(ctx,OTP_DATA_PAGE0_LOCK0_ROW+2*page,&l0) ||
       raw_read(ctx,OTP_DATA_PAGE0_LOCK0_ROW+2*page+1,&l1))return -1;
    /* Development allocation requires untouched access policy. Production sealing
     * will be separately implemented; never relax/program permanent lock rows. */
    return l0 || l1 || otp_hw->sw_lock[page]?-1:0;
}
static int raw_write(void *ctx,uint32_t row,uint32_t value) {
    unsigned page=row/64,index=row%64;
    if(!app_page(page) || index>FV_OTP_ACTIVATED_ROW || value>0xffffff || programmable(ctx,page))return -1;
    /* Root may only receive its completion marker here; secret written as ECC.
     * Token raw writes: destruction fill or fixed marker/activation constants. */
    if(page==FV_OTP_ROOT_PAGE && (index!=FV_OTP_MAGIC_ROW || value!=0x524f54))return -1;
    if(page!=FV_OTP_ROOT_PAGE && !((index<16 && value==0xffffff) ||
        (index==16 && value==0x544f4b) || (index==17 && value==0xffffff) || (index==18 && value==1)))return -1;
    return rom_func_otp_access((uint8_t*)&value,4,(otp_cmd_t){.flags=row|OTP_CMD_WRITE_BITS});
}
static int secret_read(void *ctx,unsigned page,uint8_t out[32]) {
    (void)ctx;memset(out,0,32);if(!app_page(page))return -1;
    alignas(4) uint8_t temp[32]={0};
    int r=rom_func_otp_access(temp,32,(otp_cmd_t){.flags=page*64|OTP_CMD_ECC_BITS});
    if(!r)memcpy(out,temp,32);
    mbedtls_platform_zeroize(temp,32);return r;
}
static int secret_write(void *ctx,unsigned page,const uint8_t in[32]) {
    if(programmable(ctx,page))return -1;
    for(unsigned i=0;i<64;i++){uint32_t raw;if(raw_read(ctx,page*64+i,&raw) || raw)return -1;}
    alignas(4) uint8_t temp[32];memcpy(temp,in,32);
    int r=rom_func_otp_access(temp,32,(otp_cmd_t){.flags=page*64|OTP_CMD_ECC_BITS|OTP_CMD_WRITE_BITS});
    mbedtls_platform_zeroize(temp,32);return r;
}
static int flash_read(void *ctx,uint32_t offset,uint8_t *out,size_t bytes) {
    (void)ctx;if(offset>FV_JOURNAL_BYTES || bytes>FV_JOURNAL_BYTES-offset)return -1;
    memcpy(out,(const void*)(XIP_BASE+FV_SECURITY_FLASH_OFFSET+offset),bytes);return 0;
}
typedef struct {uint32_t offset;const uint8_t *data;bool erase;} operation;
static void __not_in_flash_func(perform)(void *context) {
    operation *o=context;
    if(o->erase)flash_range_erase(FV_SECURITY_FLASH_OFFSET+o->offset,4096);
    else flash_range_program(FV_SECURITY_FLASH_OFFSET+o->offset,o->data,256);
}
static int flash_program(void *ctx,uint32_t offset,const uint8_t data[256]) {
    (void)ctx;if(offset>FV_JOURNAL_BYTES-256 || offset%256)return -1;
    operation o={offset,data,false};return flash_safe_execute(perform,&o,1000);
}
static int flash_erase(void *ctx,unsigned bank) {
    (void)ctx;if(bank>1)return -1;
    operation o={bank*4096,NULL,true};return flash_safe_execute(perform,&o,1000);
}
void fv_rp2354_authority_init(fv_enrollment_device *d) {
    pico_unique_board_id_t id;pico_get_unique_board_id(&id);
    static const char label[]="FV2/device-id/v1";
    uint8_t digest[32]={0},input[sizeof(label)+PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    memcpy(input,label,sizeof(label));memcpy(input+sizeof(label),id.id,PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    if(mbedtls_sha256(input,sizeof(input),digest,0))panic("device identity hash");
    fv_enrollment_init(d,(fv_otp_io){NULL,raw_read,raw_write,secret_read,secret_write,programmable},
        (fv_journal_io){NULL,flash_read,flash_program,flash_erase},digest);
}
