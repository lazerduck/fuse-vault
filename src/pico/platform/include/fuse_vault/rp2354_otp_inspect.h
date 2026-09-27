#ifndef FV_RP2354_OTP_INSPECT_H
#define FV_RP2354_OTP_INSPECT_H
#include <stdint.h>
typedef struct {
    uint32_t lock_raw[2],software_lock;
    int32_t lock_status[2],first_read_error;
    uint16_t blank,programmed,all_ones,unreadable;
} fv_otp_page_summary;
/* Diagnostic only, no data-row values/hashes exported. Does not write OTP,
 * supply OTP keys, or change even the resettable software locks. */
int fv_rp2354_otp_inspect(unsigned page,fv_otp_page_summary *);
#endif
