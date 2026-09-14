#ifndef FV_STORAGE_PROFILE_H
#define FV_STORAGE_PROFILE_H
#include <stdint.h>
enum { FV_PERF_SD_READ, FV_PERF_SD_WRITE, FV_PERF_SD_SYNC,
       FV_PERF_VAULT_READ, FV_PERF_VAULT_WRITE, FV_PERF_COUNT };
#if defined(PICO_ON_DEVICE) && FUSE_VAULT_HEADLESS_DEBUG
#include "pico/time.h"
uint64_t fv_storage_profile_begin(void);
void fv_storage_profile_end(unsigned kind, uint64_t start);
void fv_storage_profile_snapshot(uint32_t out[FV_PERF_COUNT * 4]);
#else
static inline uint64_t fv_storage_profile_begin(void) { return 0; }
static inline void fv_storage_profile_end(unsigned kind, uint64_t start) { (void)kind; (void)start; }
static inline void fv_storage_profile_snapshot(uint32_t out[FV_PERF_COUNT * 4]) {
    for(unsigned i=0;i<FV_PERF_COUNT*4;i++)out[i]=0;
}
#endif
#endif
