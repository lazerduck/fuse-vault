#include "fuse_vault/fido_hid.h"
#include "fuse_vault/fido_verification.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void packet(uint8_t r[64],uint32_t cid,uint8_t command,unsigned size){
    memset(r,0,64);r[0]=cid>>24;r[1]=cid>>16;r[2]=cid>>8;r[3]=cid;r[4]=command;r[5]=size>>8;r[6]=size;
}
static void hid_tests(void){
    fv_fido_hid_t h;uint8_t r[64],out[64];fv_fido_hid_reset(&h);
    packet(r,UINT32_MAX,0x86,8);memcpy(r+7,"nonce123",8);fv_fido_hid_receive(&h,r,0);
    assert(fv_fido_hid_peek(&h,out) && out[4]==0x86 && out[6]==17 && !memcmp(out+7,"nonce123",8));
    uint32_t cid=out[18];assert(cid==1 && out[23]==0x0c);fv_fido_hid_sent(&h);
    packet(r,cid,0x90,100);memset(r+7,0x55,57);fv_fido_hid_receive(&h,r,0);assert(h.receiving && !h.pending);
    packet(r,cid,1,0);fv_fido_hid_receive(&h,r,1);assert(!h.receiving && h.tx[0]==4);fv_fido_hid_sent(&h);
    packet(r,cid,0x90,100);fv_fido_hid_receive(&h,r,100);fv_fido_hid_tick(&h,3099);assert(h.receiving);
    fv_fido_hid_tick(&h,3100);assert(!h.receiving && h.tx[0]==5);fv_fido_hid_sent(&h);
    packet(r,cid,0x90,100);memset(r+7,0x55,57);fv_fido_hid_receive(&h,r,0);
    packet(r,cid,0,0);memset(r+5,0x66,43);fv_fido_hid_receive(&h,r,1);
    assert(h.pending && h.processing && h.rx_used==100 && h.rx[56]==0x55 && h.rx[57]==0x66);
    uint8_t copy[4096];memcpy(copy,h.rx,sizeof(copy));h.pending=false;
    packet(r,UINT32_MAX,0x86,8);fv_fido_hid_receive(&h,r,2);assert(h.control_pending && h.control_report[7]==6);
    assert(!memcmp(copy,h.rx,sizeof(copy)));h.control_pending=false;
    packet(r,cid+1,0x91,0);fv_fido_hid_receive(&h,r,3);assert(!h.cancelled);
    packet(r,cid,0x91,0);fv_fido_hid_receive(&h,r,4);assert(h.cancelled && !memcmp(copy,h.rx,sizeof(copy)));
    const uint8_t ok[]={0};fv_fido_hid_complete(&h,ok,1);assert(!h.processing && h.tx[0]==0x2d);
    assert(!memcmp(h.rx,(uint8_t[4096]){0},4096));fv_fido_hid_sent(&h);
    packet(r,cid,0x90,1);r[7]=4;fv_fido_hid_receive(&h,r,5);
    packet(r,cid,0x86,8);memcpy(r+7,"newnonce",8);fv_fido_hid_receive(&h,r,6);
    fv_fido_hid_complete(&h,ok,1);assert(h.tx_command==0x86 && !memcmp(h.tx,"newnonce",8));fv_fido_hid_sent(&h);
    packet(r,cid,0x90,1);r[7]=4;fv_fido_hid_receive(&h,r,7);
    for(unsigned i=0;i<4096;i++)copy[i]=(uint8_t)i;
    fv_fido_hid_complete(&h,copy,sizeof(copy));unsigned used=0,seq=0;
    while(fv_fido_hid_peek(&h,out)){
        uint8_t again[64];assert(fv_fido_hid_peek(&h,again) && !memcmp(out,again,64));
        unsigned offset=used?5:7,n=4096-used;if(n>64-offset)n=64-offset;
        if(used)assert(out[4]==seq++);assert(!memcmp(out+offset,copy+used,n));used+=n;fv_fido_hid_sent(&h);
    }assert(used==4096 && !memcmp(h.tx,(uint8_t[4096]){0},4096));
    puts("HID fragmentation, timeout, cancel, resync, busy and backpressure passed");
}
static void verification_tests(void){
    fv_fido_verification_t v={0};uint8_t a[32]={1},b[32]={2};
    assert(!fv_fido_verification_use(&v,0,a));fv_fido_verification_begin(&v,100);
    assert(fv_fido_verification_use(&v,30099,a));assert(fv_fido_verification_use(&v,600099,a));
    assert(!fv_fido_verification_use(&v,600100,a));fv_fido_verification_begin(&v,100);
    assert(!fv_fido_verification_use(&v,30100,a));fv_fido_verification_begin(&v,UINT32_MAX-10);
    assert(fv_fido_verification_use(&v,5,a));assert(!fv_fido_verification_use(&v,6,b));
    fv_fido_verification_begin(&v,1);assert(fv_fido_verification_use(&v,2,NULL));
    assert(fv_fido_verification_use(&v,3,a));assert(!fv_fido_verification_use(&v,4,b));
    fv_fido_verification_clear(&v);assert(!v.valid && !v.bound);
    fv_fido_verification_begin(&v,1);
    assert(fv_fido_verification_use_policy(&v,30000000,a,FV_FIDO_UV_SESSION));
    assert(fv_fido_verification_use_policy(&v,30000001,b,FV_FIDO_UV_SESSION));
    fv_fido_verification_clear(&v);
    assert(!fv_fido_verification_use_policy(&v,30000002,b,FV_FIDO_UV_SESSION));
    fv_fido_verification_begin(&v,1);
    assert(!fv_fido_verification_use_policy(&v,2,b,(fv_fido_uv_policy)2));
    puts("UV strict/session modes, age, RP binding, clear and clock wrap passed");
}
int main(void){hid_tests();verification_tests();}
