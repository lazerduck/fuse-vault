#ifndef FV_ENROLLMENT_H
#define FV_ENROLLMENT_H
#include "fuse_vault/journal.h"
/* Token-only OTP allocation (KeyHandling.md /
 * Improvements.md section 1):
 *
 * Pages 0-2:   reserved chip/boot configuration
 * Page 3:      status mask, 3 bits per slot (8 slots per 24-bit OTP row)
 * Pages 4-60:  token secrets, 4 tokens per page, 16 ECC rows per token
 * Pages 61-63: reserved hardware access keys and page-lock configuration
 *
 * 228 enrollment slots on a fresh token area. Never write boot/key/page-lock fuses.
 */
#define FV_OTP_MASK_PAGE 3u
#define FV_OTP_TOKEN_PAGE 4u
#define FV_OTP_TOKEN_SLOTS 228u
#define FV_OTP_TOKENS_PER_PAGE 4u
#define FV_OTP_TOKEN_ROW_STRIDE 16u   /* ECC rows per token within a page */
#define FV_OTP_SECRET_ROWS 16u
#define FV_OTP_MASK_ROWS 29u          /* ceil(228/8) = 29 mask rows on page 3 */
#define FV_OTP_SLOTS_PER_MASK_ROW 8u  /* 24 programmable bits / 3 bits per slot */
/* Mask states (3 bits per slot). OTP is write-once: transitions only add 1s,
 * so UNUSED(000) -> STARTED(100) -> READY(110) -> REVOKED(111) is valid. */
#define FV_OTP_MASK_UNUSED 0u
#define FV_OTP_MASK_STARTED 4u
#define FV_OTP_MASK_READY 6u
#define FV_OTP_MASK_REVOKED 7u
/* Interrupted STARTED revocation sets the revoke bit before completion. */
#define FV_OTP_MASK_REVOKING 5u
/* Fixed application allocation only. Never write boot/key/page-lock fuses. */
typedef struct {
    void *context;
    int (*read_raw)(void *,uint32_t row,uint32_t *);
    int (*write_raw)(void *,uint32_t row,uint32_t value);
    int (*read_secret)(void *,unsigned row,uint8_t out[32]);
    int (*write_secret)(void *,unsigned row,const uint8_t in[32]);
    int (*can_program)(void *,unsigned page);
} fv_otp_io;
typedef struct {
    fv_otp_io otp;
    fv_journal journal;
    fv_journal_io flash;
    uint8_t device_id[16];
    bool opened,terminal;
    unsigned token_slot;
} fv_enrollment_device;
void fv_enrollment_init(fv_enrollment_device *,fv_otp_io,fv_journal_io,const uint8_t device_id[16]);
void fv_enrollment_clear(fv_enrollment_device *);
/* Read-only open: 1 virgin, 2 explicit setup required (interrupted/legacy),
 * 0 current enrollment or revoked terminal state, negative on ambiguity or
 * missing/corrupt READY journal. No automatic erase, burn or attempt reset. */
int fv_enrollment_open(fv_enrollment_device *);
/* Read-only occupancy: 1 blank, 0 occupied, -1 unreadable. No secret values. */
typedef struct {int mask_blank,tokens_blank,flash_blank;} fv_enrollment_inventory;
fv_enrollment_inventory fv_enrollment_inspect(fv_enrollment_device *);
/* Read-only: 1 if the token's 16 ECC rows are all blank, 0 if occupied, -1 unreadable. */
int fv_enrollment_token_blank(fv_enrollment_device *,unsigned slot);
/* Explicit development preparation of the reserved flash banks. Refuses ANY
 * occupied/unreadable enrollment OTP row or unsupported page permissions. */
int fv_enrollment_prepare_flash(fv_enrollment_device *);
/* Explicit irreversible provisioning, or next blank token after revocation.
 * Fresh random secrets generated inside the device; no import/export. */
int fv_enrollment_provision(fv_enrollment_device *,fv_random_bytes,void *);
fv_device_authority fv_enrollment_authority(fv_enrollment_device *);
/* Explicit debug/UI request: persist destruction intent then recover/invalidate.
 * Must only be exposed via an authorized on-device UI / debug enrollment build. */
int fv_enrollment_request_destruction(fv_enrollment_device *);
#endif
