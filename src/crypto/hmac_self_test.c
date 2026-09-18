#include "fuse_vault/hmac.h"
#include <string.h>
/* Executed on the board before benchmark I/O. Hardware tags must match a
 * published HMAC vector and the independent software SHA implementation. */
bool fv_hmac_self_test(void) {
    fv_hmac h;uint8_t key[131],data[554],actual[32],reference[32];
    /* RFC 4231 case 1. */
    static const uint8_t vector[32]={0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    bool ok=false;
    memset(key,0x0b,20);
    if(fv_hmac_init(&h,key,20) || fv_hmac_compute(&h,NULL,0,(const uint8_t *)"Hi There",8,actual) || !fv_tag_equal(actual,vector))goto done;
    for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*17+3);
    for(unsigned i=0;i<sizeof(key);i++)key[i]=(uint8_t)(i*11+1);
    static const unsigned sizes[]={0,1,55,56,63,64,65,512};
    for(unsigned k=0;k<2;k++) {
        if(fv_hmac_init(&h,key,k?131:32))goto done;
        for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
            /* Unaligned storage context and payload, plus padding boundaries. */
            if(fv_hmac_compute_software(&h,data+1,40,data+41,sizes[i],reference) ||
               fv_hmac_compute(&h,data+1,40,data+41,sizes[i],actual) ||
               !fv_tag_equal(actual,reference))goto done;
        }
    }
    ok=true;
done:
    fv_hmac_clear(&h);return ok;
}
