#include "fuse_vault/storage_profile.h"
#if defined(PICO_ON_DEVICE) && FUSE_VAULT_HEADLESS_DEBUG
/* Single core, synchronous storage callbacks; no interrupts mutate these. */
static struct { uint32_t count; uint64_t us; uint32_t max_us; } stats[FV_PERF_COUNT];
uint64_t fv_storage_profile_begin(void) { return time_us_64(); }
void fv_storage_profile_end(unsigned kind, uint64_t start) {
    if(kind>=FV_PERF_COUNT)return;
    uint64_t elapsed=time_us_64()-start;
    ++stats[kind].count; stats[kind].us+=elapsed;
    uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    if(bounded>stats[kind].max_us)stats[kind].max_us=bounded;
}
void fv_storage_profile_snapshot(uint32_t out[FV_PERF_COUNT*4]) {
    for(unsigned i=0;i<FV_PERF_COUNT;i++) {
        out[i*4]=stats[i].count;out[i*4+1]=(uint32_t)stats[i].us;
        out[i*4+2]=(uint32_t)(stats[i].us>>32);out[i*4+3]=stats[i].max_us;
    }
}
#endif
