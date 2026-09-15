#include "fuse_vault/diagnostics.h"
#include <assert.h>
static uint64_t now;
uint64_t time_us_64(void){return now;}
int main(void){
 uint32_t out[FV_DIAG_WORDS];
 now=1000;uint64_t start=fv_diag_begin();now=12001000;fv_diag_end(FV_DIAG_AUTH,start);
 fv_diag_snapshot(out);assert(out[8]==1 && out[9]==12000000 && out[12]==1 && out[13]==12001);
 fv_diag_service(0);now+=5000;fv_diag_service(0);fv_diag_snapshot(out);
 assert(out[8+FV_DIAG_USB_GAP*6+3]==5000);
 fv_diag_io(4,0,512,0);fv_diag_io(5,0,512,0);fv_diag_io(6,0,512,1);
 fv_diag_snapshot(out);assert(out[2]==2 && out[3]==1 && out[5]==2);
 unsigned base=8+FV_DIAG_COUNT*6+65;
 assert(out[base]==4 && out[base+1]==2);
 for(unsigned i=0;i<150;i++)fv_diag_io(i*2,0,512,0);
 fv_diag_snapshot(out);assert(out[5]==128 && out[6]>0);
 assert(out[base+(127*5)]==298);
 start=fv_diag_begin();now+=UINT64_C(0x100000010);fv_diag_end(FV_DIAG_AUTH,start);
 fv_diag_snapshot(out);assert(out[11]==UINT32_MAX && out[10]==1);
}
