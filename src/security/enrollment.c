#include "fuse_vault/enrollment.h"
#include <mbedtls/platform_util.h>
#include <string.h>
#define ROOT_MAGIC 0x524f54u
#define TOKEN_MAGIC 0x544f4bu
static int read_row(fv_enrollment_device *d,unsigned page,unsigned row,uint32_t *out) {
    return d->otp.read_raw(d->otp.context,page*64+row,out);
}
static int blank(fv_enrollment_device *d,unsigned page) {
    for(unsigned i=0;i<64;i++){uint32_t v;if(read_row(d,page,i,&v))return -1;if(v)return 0;}
    return 1;
}
static int write_row(fv_enrollment_device *d,unsigned page,unsigned row,uint32_t value) {
    uint32_t v;
    if(read_row(d,page,row,&v))return -1;
    if(v==value)return 0;
    if(v&~value || d->otp.can_program(d->otp.context,page) ||
       d->otp.write_raw(d->otp.context,page*64+row,value) || read_row(d,page,row,&v) || v!=value)return -1;
    return 0;
}
void fv_enrollment_init(fv_enrollment_device *d,fv_otp_io otp,fv_journal_io flash,const uint8_t id[16]) {
    memset(d,0,sizeof(*d));d->otp=otp;d->flash=flash;memcpy(d->device_id,id,16);
}
void fv_enrollment_clear(fv_enrollment_device *d){mbedtls_platform_zeroize(d,sizeof(*d));}
static int open_root(fv_enrollment_device *d) {
    uint32_t mark;uint8_t root[32]={0},key[32]={0};int r=-1;
    d->opened=false;fv_journal_clear(&d->journal);
    if(read_row(d,FV_OTP_ROOT_PAGE,FV_OTP_MAGIC_ROW,&mark) || mark!=ROOT_MAGIC ||
       d->otp.read_secret(d->otp.context,FV_OTP_ROOT_PAGE,root) || fv_journal_key(root,d->device_id,key))goto done;
    fv_journal_init(&d->journal,d->flash,key,d->device_id);d->opened=true;r=0;
done:mbedtls_platform_zeroize(root,32);mbedtls_platform_zeroize(key,32);return r;
}
static int flash_blank(fv_enrollment_device *d) {
    uint8_t p[256];
    for(unsigned i=0;i<FV_JOURNAL_BYTES;i+=256) {
        if(d->flash.read(d->flash.context,i,p,256))return -1;
        for(unsigned j=0;j<256;j++)if(p[j]!=255)return 0;
    }
    return 1;
}
static int no_activated_tokens(fv_enrollment_device *d) {
    for(unsigned i=0;i<FV_OTP_TOKEN_SLOTS;i++){uint32_t active;if(read_row(d,FV_OTP_TOKEN_PAGE+i,FV_OTP_ACTIVATED_ROW,&active) || active)return -1;}
    return 0;
}
fv_enrollment_inventory fv_enrollment_inspect(fv_enrollment_device *d) {
    fv_enrollment_inventory v={blank(d,FV_OTP_ROOT_PAGE),1,flash_blank(d)};
    for(unsigned i=0;i<FV_OTP_TOKEN_SLOTS;i++) {
        int b=blank(d,FV_OTP_TOKEN_PAGE+i);
        if(b<0)v.tokens_blank=-1;
        else if(!b && v.tokens_blank==1)v.tokens_blank=0;
    }
    return v;
}
int fv_enrollment_prepare_flash(fv_enrollment_device *d) {
    fv_enrollment_inventory v=fv_enrollment_inspect(d);
    if(v.root_blank!=1 || v.tokens_blank!=1 || v.flash_blank<0)return -1;
    for(unsigned p=FV_OTP_ROOT_PAGE;p<FV_OTP_TOKEN_PAGE+FV_OTP_TOKEN_SLOTS;p++)
        if(d->otp.can_program(d->otp.context,p))return -1;
    d->opened=false;fv_journal_clear(&d->journal);
    if(v.flash_blank==1)return 0;
    for(unsigned bank=0;bank<2;bank++)if(d->flash.erase(d->flash.context,bank))return -1;
    return flash_blank(d)==1?0:-1;
}
int fv_enrollment_open(fv_enrollment_device *d) {
    if(open_root(d)) {
        if(blank(d,FV_OTP_ROOT_PAGE)!=1 || flash_blank(d)!=1)return -1;
        for(unsigned i=0;i<FV_OTP_TOKEN_SLOTS;i++)if(blank(d,FV_OTP_TOKEN_PAGE+i)!=1)return -1;
        return 1;
    }
    fv_device_state s;int result=fv_journal_load(&d->journal,&s);
    if(result==FV_JOURNAL_EMPTY)return no_activated_tokens(d)?-1:2;
    return result;
}
static int provision_page(fv_enrollment_device *d,unsigned page,uint32_t magic,fv_random_bytes rng,void *context) {
    uint8_t secret[32]={0},verify[32]={0};int r=-1;
    if(blank(d,page)!=1 || d->otp.can_program(d->otp.context,page) || rng(context,secret,32) ||
       d->otp.write_secret(d->otp.context,page,secret) || d->otp.read_secret(d->otp.context,page,verify) ||
       !fv_tag_equal(secret,verify) || write_row(d,page,FV_OTP_MAGIC_ROW,magic))goto done;
    r=0;
done:mbedtls_platform_zeroize(secret,32);mbedtls_platform_zeroize(verify,32);return r;
}
static int token_flags(fv_enrollment_device *d,unsigned slot,uint32_t *active,uint32_t *revoked) {
    uint32_t magic;
    if(slot>=FV_OTP_TOKEN_SLOTS || read_row(d,FV_OTP_TOKEN_PAGE+slot,FV_OTP_MAGIC_ROW,&magic) || magic!=TOKEN_MAGIC ||
       read_row(d,FV_OTP_TOKEN_PAGE+slot,FV_OTP_ACTIVATED_ROW,active) ||
       read_row(d,FV_OTP_TOKEN_PAGE+slot,FV_OTP_REVOKED_ROW,revoked))return -1;
    return 0;
}
int fv_enrollment_provision(fv_enrollment_device *d,fv_random_bytes rng,void *context) {
    if(!rng)return -1;
    int result=fv_enrollment_open(d);
    if(result<0)return -1;
    if(result==1) {
        /* Complete preflight before touching root fuses. */
        for(unsigned p=FV_OTP_ROOT_PAGE;p<FV_OTP_TOKEN_PAGE+FV_OTP_TOKEN_SLOTS;p++)
            if(d->otp.can_program(d->otp.context,p))return -1;
        if(provision_page(d,FV_OTP_ROOT_PAGE,ROOT_MAGIC,rng,context) || open_root(d))return -1;
    }
    fv_device_state old={0};result=fv_journal_load(&d->journal,&old);
    if(result<0)return -1;
    if(result==FV_JOURNAL_OK && old.status==FV_ENROLLMENT_EMPTY) {
        uint32_t active,revoked;
        if(token_flags(d,old.token_slot,&active,&revoked) || revoked || (active && active!=1))return -1;
        return write_row(d,FV_OTP_TOKEN_PAGE+old.token_slot,FV_OTP_ACTIVATED_ROW,1);
    }
    if(result==FV_JOURNAL_OK && old.status!=FV_ENROLLMENT_DESTROYED)return -1;
    if(result==FV_JOURNAL_EMPTY && no_activated_tokens(d))return -1;
    unsigned first=result==FV_JOURNAL_OK?old.token_slot+1:0,slot;
    for(slot=first;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        uint32_t active,revoked;
        /* A completed but never activated token can resume initial provisioning. */
        if(!token_flags(d,slot,&active,&revoked) && !active && !revoked)break;
        int b=blank(d,FV_OTP_TOKEN_PAGE+slot);if(b<0)return -1;
        if(b==1) {
            if(provision_page(d,FV_OTP_TOKEN_PAGE+slot,TOKEN_MAGIC,rng,context))return -1;
            break;
        }
        /* Partially programmed slots are consumed, never regenerated in place. */
    }
    if(slot>=FV_OTP_TOKEN_SLOTS || old.sequence==UINT64_MAX)return -1;
    fv_device_state next={.status=FV_ENROLLMENT_EMPTY,.sequence=old.sequence+1,.token_slot=slot,.policy=fv_auth_policy_default()};
    memcpy(next.device_id,d->device_id,16);
    if(fv_journal_commit(&d->journal,old.sequence,&next))return -1;
    return write_row(d,FV_OTP_TOKEN_PAGE+slot,FV_OTP_ACTIVATED_ROW,1);
}
static int load(void *context,fv_device_state *s) {
    fv_enrollment_device *d=context;
    if(!d->opened || fv_journal_load(&d->journal,s))return -1;
    uint32_t active,revoked;
    if(token_flags(d,s->token_slot,&active,&revoked) || active!=1)return -1;
    if(revoked && s->status!=FV_ENROLLMENT_DESTROY_PENDING && s->status!=FV_ENROLLMENT_DESTROYED)return -1;
    return 0;
}
static int commit(void *context,uint64_t previous,const fv_device_state *s) {
    fv_enrollment_device *d=context;
    return d->opened?fv_journal_commit(&d->journal,previous,s):-1;
}
static int binding(void *context,const uint8_t id[16],uint32_t slot,uint8_t out[32]) {
    fv_enrollment_device *d=context;fv_device_state s;uint8_t root[32]={0},token[32]={0};int r=-1;
    memset(out,0,32);
    if(load(d,&s) || s.token_slot!=slot || (s.status!=FV_ENROLLMENT_EMPTY && s.status!=FV_ENROLLMENT_ACTIVE) ||
       (s.status==FV_ENROLLMENT_ACTIVE && memcmp(id,s.volume_id,16)) ||
       d->otp.read_secret(d->otp.context,FV_OTP_ROOT_PAGE,root) || d->otp.read_secret(d->otp.context,FV_OTP_TOKEN_PAGE+slot,token))goto done;
    r=fv_vault_binding(root,token,slot,id,out);
done:mbedtls_platform_zeroize(root,32);mbedtls_platform_zeroize(token,32);return r;
}
static int destroy(void *context,uint32_t slot) {
    fv_enrollment_device *d=context;fv_device_state s;
    if(!d->opened || fv_journal_load(&d->journal,&s) || s.token_slot!=slot ||
       s.status!=FV_ENROLLMENT_DESTROY_PENDING || slot>=FV_OTP_TOKEN_SLOTS)return -1;
    unsigned page=FV_OTP_TOKEN_PAGE+slot;
    if(write_row(d,page,FV_OTP_REVOKED_ROW,0xffffff))return -1;
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++)if(write_row(d,page,i,0xffffff))return -1;
    return 0;
}
fv_device_authority fv_enrollment_authority(fv_enrollment_device *d) {
    return (fv_device_authority){d,load,commit,binding,destroy};
}
int fv_enrollment_request_destruction(fv_enrollment_device *d) {
    fv_device_state s;if(load(d,&s))return -1;
    if(s.status==FV_ENROLLMENT_DESTROYED)return 0;
    if(s.status==FV_ENROLLMENT_EMPTY || s.sequence==UINT64_MAX)return -1;
    if(s.status!=FV_ENROLLMENT_DESTROY_PENDING) {
        uint64_t previous=s.sequence;s.sequence++;s.status=FV_ENROLLMENT_DESTROY_PENDING;s.attempt_pending=false;
        if(commit(d,previous,&s))return -1;
    }
    if(destroy(d,s.token_slot) || s.sequence==UINT64_MAX)return -1;
    uint64_t previous=s.sequence;s.sequence++;s.status=FV_ENROLLMENT_DESTROYED;
    return commit(d,previous,&s);
}
