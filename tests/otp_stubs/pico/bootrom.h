#ifndef FV_OTP_TEST_BOOTROM_H
#define FV_OTP_TEST_BOOTROM_H
#include <stdint.h>
typedef struct {uint32_t flags;} otp_cmd_t;
int rom_func_otp_access(uint8_t *,uint32_t,otp_cmd_t);
#endif
