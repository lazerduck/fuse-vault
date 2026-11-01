#include "fuse_vault/security.h"
#include <string.h>
fv_auth_policy fv_auth_policy_default(void) {return (fv_auth_policy){10,FV_LIMIT_DESTROY,300,0};}
bool fv_auth_policy_encode(const fv_auth_policy *p,uint8_t out[16]) {
    if(!out)return false;
    memset(out,0,16);
    if(!p)return false;
    if(p->limit_action==FV_LIMIT_NONE) {
        if(p->max_attempts)return false;
    } else if(!p->max_attempts || (p->limit_action!=FV_LIMIT_DESTROY && p->limit_action!=FV_LIMIT_LOCKOUT))return false;
    switch(p->auto_lock_seconds) {
    case 0:case 60:case 120:case 300:case 600:case 900:case 1800:break;
    default:return false;
    }
    switch(p->screen_sleep_seconds) {
    case 0:case 30:case 60:case 120:case 300:case 600:case UINT16_MAX:break;
    default:return false;
    }
    out[0]=1;out[2]=16;
    for(unsigned i=0;i<4;i++)out[4+i]=(uint8_t)(p->max_attempts>>(8*i));
    out[8]=(uint8_t)p->limit_action;
    for(unsigned i=0;i<4;i++)out[10+i]=(uint8_t)(p->auto_lock_seconds>>(8*i));
    out[14]=(uint8_t)p->screen_sleep_seconds;
    out[15]=(uint8_t)(p->screen_sleep_seconds>>8);
    return true;
}
bool fv_auth_policy_decode(const uint8_t *in,size_t bytes,fv_auth_policy *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!in || bytes!=16)return false;
    fv_auth_policy p={0};uint8_t encoded[16];
    for(unsigned i=0;i<4;i++)p.max_attempts|=(uint32_t)in[4+i]<<(8*i);
    p.limit_action=(fv_limit_action)((unsigned)in[8]|((unsigned)in[9]<<8));
    for(unsigned i=0;i<4;i++)p.auto_lock_seconds|=(uint32_t)in[10+i]<<(8*i);
    p.screen_sleep_seconds=(uint16_t)((unsigned)in[14]|((unsigned)in[15]<<8));
    if(!fv_auth_policy_encode(&p,encoded) || memcmp(in,encoded,16))return false;
    *out=p;return true;
}
