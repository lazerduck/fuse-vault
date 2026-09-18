#ifndef FV_RP2354_AUTHORITY_H
#define FV_RP2354_AUTHORITY_H
#include "fuse_vault/enrollment.h"
#define FV_SECURITY_FLASH_OFFSET 0x001fe000u
/* Init sets adapters/identity only; open reads OTP/journal, never auto-provisions. */
void fv_rp2354_authority_init(fv_enrollment_device *);
#endif
