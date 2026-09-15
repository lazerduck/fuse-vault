#ifndef FV_BASELINE_BENCH_H
#define FV_BASELINE_BENCH_H
#include "fuse_vault/rp2354_sd.h"
void fv_baseline_bench_request(void);
/* Enter only from a locked session; once entered, remain until power cycle. */
bool fv_baseline_bench_poll(fv_rp2354_sd_t *sd, bool allowed,
                            void (*service)(void));
#endif
