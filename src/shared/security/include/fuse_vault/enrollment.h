#ifndef FV_ENROLLMENT_H
#define FV_ENROLLMENT_H
#include "fuse_vault/journal.h"
#define FV_OTP_ROOT_PAGE 16u
#define FV_OTP_TOKEN_PAGE 17u
#define FV_OTP_TOKEN_SLOTS 8u
#define FV_OTP_SECRET_ROWS 16u
#define FV_OTP_MAGIC_ROW 16u
#define FV_OTP_REVOKED_ROW 17u
#define FV_OTP_ACTIVATED_ROW 18u
/* Fixed application allocation only. Never write boot/key/page-lock fuses. */
typedef struct {
    void *context;
    int (*read_raw)(void *,uint32_t row,uint32_t *);
    int (*write_raw)(void *,uint32_t row,uint32_t value);
    int (*read_secret)(void *,unsigned page,uint8_t out[32]);
    int (*write_secret)(void *,unsigned page,const uint8_t in[32]);
    int (*can_program)(void *,unsigned page);
} fv_otp_io;
typedef struct {
    fv_otp_io otp;
    fv_journal journal;
    fv_journal_io flash;
    uint8_t device_id[16];
    bool opened;
} fv_enrollment_device;
void fv_enrollment_init(fv_enrollment_device *,fv_otp_io,fv_journal_io,const uint8_t device_id[16]);
void fv_enrollment_clear(fv_enrollment_device *);
/* Read-only root/journal open. Returns 1 for entirely virgin allocation, 2 for
 * incomplete first provisioning; negatives for failure. No automatic reset. */
int fv_enrollment_open(fv_enrollment_device *);
/* Read-only occupancy: 1 blank, 0 occupied, -1 unreadable. No secret values. */
typedef struct {int root_blank,tokens_blank,flash_blank;} fv_enrollment_inventory;
fv_enrollment_inventory fv_enrollment_inspect(fv_enrollment_device *);
/* Explicit development preparation of the reserved flash banks. Refuses ANY
 * occupied/unreadable enrollment OTP row or unsupported page permissions. */
int fv_enrollment_prepare_flash(fv_enrollment_device *);
/* Explicit irreversible provisioning, or next blank token after DESTROYED.
 * Fresh random secrets generated inside the device; no import/export. */
int fv_enrollment_provision(fv_enrollment_device *,fv_random_bytes,void *);
fv_device_authority fv_enrollment_authority(fv_enrollment_device *);
/* Explicit debug/UI request: persist destruction intent then recover/invalidate.
 * Must only be exposed via an authorized on-device UI / debug enrollment build. */
int fv_enrollment_request_destruction(fv_enrollment_device *);
#endif
