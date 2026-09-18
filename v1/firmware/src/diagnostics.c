#include "fuse_vault/diagnostics.h"
#if defined(PICO_ON_DEVICE) && FUSE_VAULT_HEADLESS_DEBUG
#include "pico/time.h"
#include <string.h>
/* Single main-core writer. No sector contents, passwords or keys recorded. */
static struct {uint32_t count,max,first_ms,last_ms;uint64_t total;} timing[FV_DIAG_COUNT];
static uint64_t service[3];
static uint32_t runs[FV_DIAG_RUNS][5], bins[65], run_count, overwritten, reads,writes,partial;
void fv_diag_reset(void){
 memset(timing,0,sizeof(timing));memset(service,0,sizeof(service));
 memset(runs,0,sizeof(runs));memset(bins,0,sizeof(bins));
 run_count=overwritten=reads=writes=partial=0;
}
uint64_t fv_diag_begin(void){return time_us_64();}
void fv_diag_end(unsigned k,uint64_t start){
 if(k>=FV_DIAG_COUNT)return;
 uint64_t now=time_us_64(),elapsed=now-start;
 if(!timing[k].count)timing[k].first_ms=(uint32_t)(start/1000);
 timing[k].count++;timing[k].total+=elapsed;timing[k].last_ms=(uint32_t)(now/1000);
 uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
 if(bounded>timing[k].max)timing[k].max=bounded;
}
void fv_diag_service(unsigned k){
 if(k>=3)return;
 uint64_t now=time_us_64();
 if(service[k])fv_diag_end(FV_DIAG_USB_GAP+k,service[k]);
 service[k]=now;
}
void fv_diag_io(uint32_t lba,uint32_t offset,uint32_t bytes,int write){
 if(write)writes++;else reads++;
 if(offset || bytes!=512)partial++;
 if(!write)bins[lba/1024<64?lba/1024:64]++;
 uint32_t now=(uint32_t)(time_us_64()/1000);
 if(run_count && !offset && bytes==512){
  uint32_t *last=runs[(run_count-1)%FV_DIAG_RUNS];
  if(last[4]==(uint32_t)write && last[0]+last[1]==lba){last[1]++;last[3]=now;return;}
 }
 uint32_t *r=runs[run_count%FV_DIAG_RUNS];
 if(run_count>=FV_DIAG_RUNS)overwritten++;
 r[0]=lba;r[1]=1;r[2]=now;r[3]=now;r[4]=(uint32_t)write | ((offset || bytes!=512)?2u:0u);run_count++;
}
void fv_diag_snapshot(uint32_t out[FV_DIAG_WORDS]){
 memset(out,0,FV_DIAG_WORDS*4);
 out[0]=1;out[1]=(uint32_t)(time_us_64()/1000);out[2]=reads;out[3]=writes;out[4]=partial;
 unsigned n=run_count<FV_DIAG_RUNS?run_count:FV_DIAG_RUNS;
 out[5]=n;out[6]=overwritten;out[7]=FV_DIAG_COUNT;
 unsigned p=8;
 for(unsigned i=0;i<FV_DIAG_COUNT;i++){
  out[p++]=timing[i].count;out[p++]=(uint32_t)timing[i].total;out[p++]=(uint32_t)(timing[i].total>>32);
  out[p++]=timing[i].max;out[p++]=timing[i].first_ms;out[p++]=timing[i].last_ms;
 }
 memcpy(out+p,bins,sizeof(bins));p+=65;
 for(unsigned i=0;i<n;i++){memcpy(out+p,runs[(run_count-n+i)%FV_DIAG_RUNS],20);p+=5;}
}
#endif
