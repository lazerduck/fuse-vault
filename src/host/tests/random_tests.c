#include "fuse_vault/random.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool zero(const void *p,size_t n){const uint8_t *b=p;while(n--)if(*b++)return false;return true;}
typedef struct {unsigned calls,fail;bool constant,all_zero,all_ones;} source_state;
static int source(void *context,uint8_t out[24]) {
    source_state *s=context;s->calls++;
    for(unsigned i=0;i<24;i++)out[i]=s->all_zero?0:s->all_ones?255:(uint8_t)(i+(s->constant?1:s->calls));
    return s->calls==s->fail?-1:0;
}
int main(void) {
    CHECK(!mbedtls_ctr_drbg_self_test(0));
    fv_random r={0};source_state s={0};uint8_t a[32],b[32];
    CHECK(!fv_random_init(&r,source,&s));CHECK(s.calls==2 && r.blocks==2);
    CHECK(!fv_random_generate(&r,a,32));CHECK(s.calls==4);
    CHECK(!fv_random_generate(&r,b,32));CHECK(s.calls==6);CHECK(memcmp(a,b,32));
    s.fail=7;memset(a,1,32);CHECK(fv_random_generate(&r,a,32));CHECK(zero(a,32));CHECK(r.failed && !r.ready);
    s.fail=0;CHECK(fv_random_generate(&r,b,32));CHECK(zero(b,32));CHECK(s.calls==7);
    fv_random_clear(&r);CHECK(zero(&r,sizeof(r)));
    for(unsigned mode=0;mode<4;mode++) {
        s=(source_state){.constant=mode==0,.all_zero=mode==1,.all_ones=mode==2,.fail=mode==3?2:0};
        CHECK(fv_random_init(&r,source,&s));CHECK(r.failed && !r.ready);
        CHECK(fv_random_generate(&r,a,32));CHECK(zero(a,32));fv_random_clear(&r);
    }
    puts("CTR-DRBG: library KAT, fresh-entropy reseeding, repeated/constant/source failure and output erasure passed");
    return 0;
}
