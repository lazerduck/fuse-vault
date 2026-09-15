#ifndef FV_DIAGNOSTICS_H
#define FV_DIAGNOSTICS_H
#include <stdint.h>
enum {FV_DIAG_AUTH,FV_DIAG_PBKDF,FV_DIAG_KDF_KMAC,FV_DIAG_ACTIVATE,FV_DIAG_ATTACH,
 FV_DIAG_USB,FV_DIAG_INPUT,FV_DIAG_DISPLAY,FV_DIAG_DEBUG,FV_DIAG_USB_GAP,FV_DIAG_INPUT_GAP,FV_DIAG_DISPLAY_GAP,FV_DIAG_PRESENT,FV_DIAG_COUNT};
#define FV_DIAG_RUNS 128u
#define FV_DIAG_WORDS (8u + FV_DIAG_COUNT*6u + 65u + FV_DIAG_RUNS*5u)
#if defined(PICO_ON_DEVICE) && FUSE_VAULT_HEADLESS_DEBUG
void fv_diag_reset(void);
uint64_t fv_diag_begin(void);
void fv_diag_end(unsigned kind,uint64_t start);
void fv_diag_service(unsigned kind);
void fv_diag_io(uint32_t lba,uint32_t offset,uint32_t bytes,int write);
void fv_diag_snapshot(uint32_t out[FV_DIAG_WORDS]);
#else
static inline void fv_diag_reset(void){}
static inline uint64_t fv_diag_begin(void){return 0;}
static inline void fv_diag_end(unsigned k,uint64_t s){(void)k;(void)s;}
static inline void fv_diag_service(unsigned k){(void)k;}
static inline void fv_diag_io(uint32_t l,uint32_t o,uint32_t b,int w){(void)l;(void)o;(void)b;(void)w;}
static inline void fv_diag_snapshot(uint32_t out[FV_DIAG_WORDS]){for(unsigned i=0;i<FV_DIAG_WORDS;i++)out[i]=0;}
#endif
#endif
