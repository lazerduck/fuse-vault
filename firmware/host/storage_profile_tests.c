#include "fuse_vault/storage_profile.h"
#include <assert.h>
static uint64_t now;
uint64_t time_us_64(void) { return now; }
int main(void) {
    uint32_t output[FV_PERF_COUNT*4];
    uint64_t outer=fv_storage_profile_begin();now=100;
    uint64_t inner=fv_storage_profile_begin();now=1100;
    fv_storage_profile_end(FV_PERF_SD_READ,inner);now=1500;
    fv_storage_profile_end(FV_PERF_VAULT_READ,outer);
    fv_storage_profile_snapshot(output);
    assert(output[FV_PERF_SD_READ*4]==1 && output[FV_PERF_SD_READ*4+1]==1000);
    assert(output[FV_PERF_VAULT_READ*4]==1 && output[FV_PERF_VAULT_READ*4+1]==1500);
    assert(output[FV_PERF_SD_WRITE*4]==0);
    inner=fv_storage_profile_begin();now+=UINT64_C(0x100000010);
    fv_storage_profile_end(FV_PERF_SD_READ,inner);
    fv_storage_profile_snapshot(output);
    assert(output[0]==2 && output[1]==1016 && output[2]==1 && output[3]==UINT32_MAX);
}
