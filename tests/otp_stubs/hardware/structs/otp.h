#ifndef FV_OTP_TEST_STRUCT_H
#define FV_OTP_TEST_STRUCT_H
#include <stdint.h>
typedef struct {uint32_t sw_lock[64];} otp_hw_t;
extern otp_hw_t fake_otp;
#define otp_hw (&fake_otp)
#endif
