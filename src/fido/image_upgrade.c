#include "image_upgrade.h"
#include <string.h>

#define OLD_BYTES 65536u
#define ADDRESS_BASE 4096u
#define DELTA (FV_FIDO_STORE_BYTES-OLD_BYTES)
#define ROM_END (OLD_BYTES-12u)
#define ROM_START (ROM_END-16384u)
#define DATA_END (ROM_START-8u)
_Static_assert(FV_FIDO_STORE_BYTES==131072u,"upgrade targets the 128 KiB layout");
static uint32_t get(const uint8_t *p,unsigned n){
    uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(8*i);return v;
}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static bool valid_list(const uint8_t *image,uint32_t start,uint32_t end){
    uint32_t previous=end,next=get(image+end,4);
    while(next){
        if(next<ADDRESS_BASE+start || next>=ADDRESS_BASE+previous)return false;
        uint32_t offset=next-ADDRESS_BASE;
        if(previous-offset<12u || get(image+offset+4,4)!=ADDRESS_BASE+previous)return false;
        uint32_t length=get(image+offset+10,2),header=12;
        if(length==65535u){
            if(previous-offset<16u)return false;
            length=get(image+offset+12,4);header=16;
        }
        if(length>previous-offset-header)return false;
        previous=offset;next=get(image+offset,4);
    }
    return true;
}
static void relocate(uint8_t *image,uint32_t end){
    uint32_t next=get(image+end,4);
    if(next)put(image+end,next+DELTA);
    while(next){
        uint32_t offset=next-ADDRESS_BASE;
        next=get(image+offset,4);
        put(image+offset,next?next+DELTA:0);
        put(image+offset+4,get(image+offset+4,4)+DELTA);
    }
}
bool fv_fido_image_upgrade(uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!image)return false;
    unsigned i=0;while(i<OLD_BYTES && image[i]==255)i++;
    if(i==OLD_BYTES){memset(image,255,FV_FIDO_STORE_BYTES);return true;}
    if(!valid_list(image,0,DATA_END) || !valid_list(image,ROM_START,ROM_END))return false;
    /* File payloads identify objects by ID, not by storage address. Only the
     * next/previous links need relocation; all credential payloads stay exact. */
    relocate(image,DATA_END);relocate(image,ROM_END);
    memmove(image+DELTA,image,OLD_BYTES);
    memset(image,255,DELTA);
    return true;
}
