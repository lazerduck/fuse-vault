#include "fuse_vault/rp2354_entropy.h"
#include "hardware/structs/trng.h"
#include "pico/time.h"
#include <string.h>
/* Development configuration based on RP2350 datasheet 12.12: chain 0, sample
 * period 100 clocks, built-in VN/CRNGT/autocorrelation enabled. Must characterize
 * on the actual board before qualifying as the production entropy source. */
void fv_rp2354_entropy_init(fv_rp2354_entropy *e) {
    memset(e,0,sizeof(*e));
    trng_hw->rnd_source_enable=0;
    trng_hw->trng_sw_reset=1;
    (void)trng_hw->trng_sw_reset;(void)trng_hw->trng_sw_reset;
    trng_hw->rng_imr=15;
    trng_hw->trng_config=0;
    trng_hw->sample_cnt1=100;
    trng_hw->trng_debug_control=0;
    trng_hw->rng_debug_en_input=0;
    trng_hw->rng_icr=15;
}
int fv_rp2354_entropy_block(void *context,uint8_t out[24]) {
    fv_rp2354_entropy *e=context;memset(out,0,24);
    if(e->failed)return -1;
    uint64_t start=time_us_64();int result=-1;
    /* Detect accidental interference from another driver, fail rather than
     * silently accepting an untested configuration/bypassed health checks. */
    if(trng_hw->trng_debug_control || trng_hw->rng_debug_en_input ||
       trng_hw->sample_cnt1!=100 || trng_hw->trng_config!=0)goto done;
    trng_hw->rng_icr=1;
    trng_hw->rnd_source_enable=1;
    for(;;) {
        uint32_t status=trng_hw->rng_isr;e->last_status=status;
        if(status&14)goto done;
        if(status&1)break;
        if(time_us_64()-start>=1000000){e->timeouts++;goto done;}
        tight_loop_contents();
    }
    /* Reading EHR[5] clears the result. Disable generation immediately after. */
    for(unsigned i=0;i<6;i++) {
        uint32_t word=trng_hw->ehr_data[i];
        for(unsigned j=0;j<4;j++)out[4*i+j]=(uint8_t)(word>>(8*j));
    }
    result=0;e->blocks++;
done:
    trng_hw->rnd_source_enable=0;
    e->autocorrelation=trng_hw->autocorr_statistic;
    e->elapsed_us+=time_us_64()-start;
    if(result){e->failed=true;e->failures++;memset(out,0,24);}
    return result;
}
