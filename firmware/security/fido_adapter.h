#ifndef FV_FIDO_ADAPTER_H
#define FV_FIDO_ADAPTER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "device_ui.h"
#include "fuse_vault/vault.h"
void fv_fido_poll(void);
void fv_fido_disconnect(void);
bool fv_fido_busy(void);
bool fv_fido_disk_available(void);
bool fv_usb_worker_idle(void);
bool fv_fido_cancelled(void *);
void fv_fido_ui_poll(fv_ui *);
void fv_fido_execute(void);
void fv_fido_close(void);
void fv_fido_unlocked(void);
int fv_fido_initialize(void);
uint8_t fv_fido_policy_get(void);
int fv_fido_policy_set(uint8_t);
/* Single worker adapters; pump only storage while a modal prompt waits. */
fv_vault *fv_fido_vault(void);
bool fv_fido_random(void *,uint8_t *,size_t);
int fv_fido_verify_secret(const uint8_t *,size_t);
void fv_fido_pump(void);
uint16_t fv_fido_profile(void);
#endif
