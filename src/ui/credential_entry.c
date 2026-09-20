#include "credential_entry.h"
#include "device_ui.h"
#include <string.h>
/* Stable V1 word-list order; these indices are the profile-4 wire encoding. */
static const char *const words[64] = {
    "amber", "apple", "atlas", "baker", "beach", "birch", "bloom", "brave",
    "cedar", "charm", "cloud", "coral", "delta", "dream", "eagle", "ember",
    "fable", "field", "flame", "flora", "frost", "giant", "globe", "grape",
    "haven", "hazel", "honey", "ivory", "jolly", "karma", "lemon", "light",
    "lunar", "maple", "metal", "mint", "noble", "north", "ocean", "olive",
    "orbit", "pearl", "piano", "pixel", "prism", "queen", "quiet", "raven",
    "river", "robin", "solar", "spark", "stone", "storm", "tiger", "trail",
    "union", "valley", "vivid", "water", "whale", "willow", "world", "zebra",
};

const char *fv_entry_word(unsigned index){return index<64?words[index]:"?";}
void fv_entry_begin(fv_credential_entry *e,uint16_t profile){memset(e,0,sizeof(*e));e->profile=profile;}
int fv_entry_key(fv_credential_entry *e,unsigned key){
    if(e->profile==3){
        if(key==UI_UP)e->values[e->selected]=(uint8_t)((e->values[e->selected]+1)%100);
        else if(key==UI_DOWN)e->values[e->selected]=(uint8_t)((e->values[e->selected]+99)%100);
        else if(key==UI_LEFT)e->selected=(uint8_t)((e->selected+3)%4);
        else if(key==UI_RIGHT)e->selected=(uint8_t)((e->selected+1)%4);
        else if(key==UI_SELECT)return 1;
        else if(key==UI_BACK)return -1;
    }else if(e->profile==4){
        if(key==UI_BACK){
            if(e->depth){--e->depth;e->prefix/=4;}
            else if(e->count)e->values[--e->count]=0;
            else return -1;
        }else if(key==UI_SELECT){if(e->count==4)return 1;}
        else if(key>=UI_UP && key<=UI_RIGHT){
            unsigned choice=key==UI_UP?0:key==UI_RIGHT?1:key==UI_DOWN?2:3;
            if(e->count==4){e->count=(uint8_t)choice;memset(e->values+choice,0,4-choice);}
            else if(e->depth<2){e->prefix=(uint8_t)(e->prefix*4+choice);++e->depth;}
            else {e->values[e->count++]=(uint8_t)(e->prefix*4+choice);e->depth=e->prefix=0;}
        }
    }
    return 0;
}
