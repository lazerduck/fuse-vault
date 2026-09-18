#include "fuse_vault/hmac.h"
#include "pico/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d\n",__LINE__);exit(1);}}while(0)
int main(void) {
    CHECK(fv_hmac_backend()==1 && fv_hmac_self_test());
    CHECK(!fake_busy && fake_starts==fake_finishes);
    fv_hmac h;uint8_t key[32]={1},data[512]={3},tag[32];
    CHECK(!fv_hmac_init(&h,key,32));
    unsigned finished=fake_finishes;
    fake_busy=true;memset(tag,0xa5,32);
    CHECK(!fv_hmac_self_test() && fake_busy);
    CHECK(fv_hmac_compute(&h,NULL,0,data,512,tag)!=0 && fake_busy && fake_finishes==finished);
    for(unsigned i=0;i<32;i++)CHECK(tag[i]==0);
    fake_busy=false;
    fake_fail_start=fake_starts+2; /* Inner finishes; outer lock acquisition fails. */
    CHECK(fv_hmac_compute(&h,NULL,0,data,512,tag)!=0 && !fake_busy && fake_finishes==finished+1);
    fake_fail_start=0;fake_error=true;unsigned abandons=fake_abandons;
    CHECK(fv_hmac_compute(&h,NULL,0,data,512,tag)!=0 && !fake_busy && fake_abandons==abandons+1);
    fake_error=false;
    CHECK(!fv_hmac_compute(&h,NULL,0,data,512,tag) && !fake_busy);
    unsigned starts=fake_starts;
    CHECK(fv_hmac_compute(&h,NULL,1,data,512,tag)!=0 && fake_starts==starts);
    fv_hmac_clear(&h);
    CHECK(fv_hmac_compute(&h,NULL,0,data,512,tag)!=0 && fake_starts==starts);
    puts("PASS: Pico HMAC dispatch, vectors, ownership, lock contention, failure cleanup, reuse");
    return 0;
}
