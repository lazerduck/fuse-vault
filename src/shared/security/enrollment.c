#include "fuse_vault/enrollment.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static unsigned token_page(unsigned slot){return FV_OTP_TOKEN_PAGE+slot/FV_OTP_TOKENS_PER_PAGE;}
static unsigned token_row_offset(unsigned slot){return (slot%FV_OTP_TOKENS_PER_PAGE)*FV_OTP_TOKEN_ROW_STRIDE;}
static unsigned mask_row(unsigned slot){return slot/FV_OTP_SLOTS_PER_MASK_ROW;}
static unsigned mask_bit(unsigned slot){return (slot%FV_OTP_SLOTS_PER_MASK_ROW)*3u;}
static int read_row(fv_enrollment_device *d,unsigned page,unsigned row,uint32_t *out) {
    return d->otp.read_raw(d->otp.context,page*64+row,out);
}
static int blank(fv_enrollment_device *d,unsigned page) {
    for(unsigned i=0;i<64;i++){uint32_t v;if(read_row(d,page,i,&v))return -1;if(v)return 0;}
    return 1;
}
/* Occupancy is a raw-row property: ECC decoding may fail on legacy rows. */
static int token_blank(fv_enrollment_device *d,unsigned slot) {
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++) {
        uint32_t raw;
        if(read_row(d,token_page(slot),token_row_offset(slot)+i,&raw))return -1;
        if(raw)return 0;
    }
    return 1;
}
int fv_enrollment_token_blank(fv_enrollment_device *d,unsigned slot) {
    if(slot>=FV_OTP_TOKEN_SLOTS)return -1;
    return token_blank(d,slot);
}
static int write_row(fv_enrollment_device *d,unsigned page,unsigned row,uint32_t value) {
    uint32_t v;
    if(read_row(d,page,row,&v))return -1;
    if(v==value)return 0;
    if(v&~value || d->otp.can_program(d->otp.context,page) ||
       d->otp.write_raw(d->otp.context,page*64+row,value) || read_row(d,page,row,&v) || v!=value)return -1;
    return 0;
}
/* 3-bit mask state for a slot, packed 8 slots per 24-bit OTP row on page 3. */
static int mask_read(fv_enrollment_device *d,unsigned slot,uint32_t *state) {
    uint32_t v;
    if(slot>=FV_OTP_TOKEN_SLOTS || read_row(d,FV_OTP_MASK_PAGE,mask_row(slot),&v))return -1;
    *state=(v>>mask_bit(slot))&0x7u;
    return 0;
}
static int mask_write(fv_enrollment_device *d,unsigned slot,uint32_t state) {
    uint32_t v;
    if(slot>=FV_OTP_TOKEN_SLOTS || read_row(d,FV_OTP_MASK_PAGE,mask_row(slot),&v))return -1;
    uint32_t current=(v>>mask_bit(slot))&0x7u;
    if(current & ~state)return -1; /* OTP write-once: cannot clear bits */
    uint32_t newv=(v & ~(0x7u<<mask_bit(slot))) | (state<<mask_bit(slot));
    if(v==newv)return 0;
    if(d->otp.can_program(d->otp.context,FV_OTP_MASK_PAGE) ||
       d->otp.write_raw(d->otp.context,FV_OTP_MASK_PAGE*64+mask_row(slot),newv) ||
       read_row(d,FV_OTP_MASK_PAGE,mask_row(slot),&v) || v!=newv)return -1;
    return 0;
}
/* Read a token's 32-byte secret via the ECC-protected interface at the slot's
 * row offset within its page. */
static int read_token(fv_enrollment_device *d,unsigned slot,uint8_t out[32]) {
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    return d->otp.read_secret(d->otp.context,row,out);
}
/* Only this slot's 16 raw rows must be blank; adjacent slots may be used. */
static int write_token(fv_enrollment_device *d,unsigned slot,const uint8_t in[32]) {
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    if(token_blank(d,slot)!=1 || d->otp.can_program(d->otp.context,token_page(slot)))return -1;
    return d->otp.write_secret(d->otp.context,row,in);
}
void fv_enrollment_init(fv_enrollment_device *d,fv_otp_io otp,fv_journal_io flash,const uint8_t id[16]) {
    memset(d,0,sizeof(*d));d->otp=otp;d->flash=flash;memcpy(d->device_id,id,16);
}
void fv_enrollment_clear(fv_enrollment_device *d){mbedtls_platform_zeroize(d,sizeof(*d));}
/* Scan the entire mask before selecting anything. Malformed states or more
 * than one READY token are ambiguous and must never select a journal. */
typedef struct {int ready,last_revoked;bool started;} mask_inventory;
static int inspect_mask(fv_enrollment_device *d,mask_inventory *v) {
    *v=(mask_inventory){.ready=-1,.last_revoked=-1};
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        uint32_t state;
        if(mask_read(d,slot,&state))return -1;
        switch(state) {
        case FV_OTP_MASK_UNUSED:break;
        case FV_OTP_MASK_STARTED:
        case FV_OTP_MASK_REVOKING:v->started=true;break;
        case FV_OTP_MASK_REVOKED:v->last_revoked=(int)slot;break;
        case FV_OTP_MASK_READY:
            if(v->ready>=0)return -1;
            v->ready=(int)slot;break;
        default:return -1;
        }
    }
    return v->ready>=0 && v->started?-1:0;
}
static int open_token(fv_enrollment_device *d,unsigned slot) {
    uint8_t token[32]={0},key[32]={0};int r=-1;
    d->opened=false;d->terminal=false;fv_journal_clear(&d->journal);
    if(read_token(d,slot,token) || fv_journal_key(token,d->device_id,slot,key))goto done;
    fv_journal_init(&d->journal,d->flash,key,d->device_id);
    d->token_slot=slot;d->opened=true;r=0;
done:mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return r;
}
static int burned(fv_enrollment_device *d,unsigned slot) {
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++) {
        uint32_t raw;
        if(read_row(d,token_page(slot),token_row_offset(slot)+i,&raw))return -1;
        if(raw!=0xffffff)return 0;
    }
    return 1;
}
static int finish_burn(fv_enrollment_device *d,unsigned slot) {
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++)
        if(write_row(d,token_page(slot),token_row_offset(slot)+i,0xffffff))return -1;
    return 0;
}
static int load(void *context,fv_device_state *s);
static int flash_blank(fv_enrollment_device *d) {
    uint8_t p[256];
    for(unsigned i=0;i<FV_JOURNAL_BYTES;i+=256) {
        if(d->flash.read(d->flash.context,i,p,256))return -1;
        for(unsigned j=0;j<256;j++)if(p[j]!=255)return 0;
    }
    return 1;
}
fv_enrollment_inventory fv_enrollment_inspect(fv_enrollment_device *d) {
    fv_enrollment_inventory v={blank(d,FV_OTP_MASK_PAGE),1,flash_blank(d)};
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        int b=token_blank(d,slot);
        if(b<0)v.tokens_blank=-1;
        else if(!b && v.tokens_blank==1)v.tokens_blank=0;
    }
    return v;
}
int fv_enrollment_prepare_flash(fv_enrollment_device *d) {
    fv_enrollment_inventory v=fv_enrollment_inspect(d);
    if(v.mask_blank!=1 || v.tokens_blank!=1 || v.flash_blank<0)return -1;
    for(unsigned p=FV_OTP_MASK_PAGE;p<FV_OTP_TOKEN_PAGE+(FV_OTP_TOKEN_SLOTS+FV_OTP_TOKENS_PER_PAGE-1)/FV_OTP_TOKENS_PER_PAGE;p++)
        if(d->otp.can_program(d->otp.context,p))return -1;
    d->opened=false;fv_journal_clear(&d->journal);
    if(v.flash_blank==1)return 0;
    for(unsigned bank=0;bank<2;bank++)if(d->flash.erase(d->flash.context,bank))return -1;
    return flash_blank(d)==1?0:-1;
}
int fv_enrollment_open(fv_enrollment_device *d) {
    mask_inventory v;
    d->opened=false;d->terminal=false;fv_journal_clear(&d->journal);
    if(inspect_mask(d,&v))return -1;
    if(v.ready>=0) {
        if(open_token(d,(unsigned)v.ready))return -1;
        fv_device_state s;
        return load(d,&s); /* READY with missing/corrupt journal always denies. */
    }
    if(v.started)return 2; /* Explicit setup abandons interrupted provisioning. */
    if(v.last_revoked>=0) {
        d->terminal=true;d->token_slot=(unsigned)v.last_revoked;
        return 0; /* OTP alone authorizes finishing destruction after reboot. */
    }
    fv_enrollment_inventory inventory=fv_enrollment_inspect(d);
    if(inventory.mask_blank<0 || inventory.tokens_blank<0 || inventory.flash_blank<0)return -1;
    if(inventory.tokens_blank==0)return 2; /* Exclude legacy occupied rows. */
    return inventory.flash_blank==1?1:-1;
}
/* Only explicit provisioning with no READY token may erase the old journal.
 * STARTED -> secret -> fresh journal -> READY makes every cut unambiguous:
 * STARTED is abandoned, while READY always requires its existing journal. */
int fv_enrollment_provision(fv_enrollment_device *d,fv_random_bytes rng,void *context) {
    mask_inventory v;
    if(!rng || inspect_mask(d,&v))return -1;
    if(v.ready>=0) {
        fv_device_state s;
        if(fv_enrollment_open(d) || load(d,&s))return -1;
        return s.status==FV_ENROLLMENT_EMPTY?0:-1;
    }
    d->opened=false;d->terminal=false;fv_journal_clear(&d->journal);
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        uint32_t state;
        if(mask_read(d,slot,&state))return -1;
        if(state==FV_OTP_MASK_STARTED) {
            /* Never transiently expose abandoned provisioning as READY. */
            if(mask_write(d,slot,FV_OTP_MASK_REVOKING))return -1;
            state=FV_OTP_MASK_REVOKING;
        }
        if(state==FV_OTP_MASK_REVOKING) {
            if(mask_write(d,slot,FV_OTP_MASK_REVOKED))return -1;
            state=FV_OTP_MASK_REVOKED;
        }
        if(state==FV_OTP_MASK_REVOKED && finish_burn(d,slot))return -1;
    }
    unsigned slot;
    for(slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        uint32_t state;
        if(mask_read(d,slot,&state))return -1;
        if(state!=FV_OTP_MASK_UNUSED)continue;
        int b=token_blank(d,slot);
        if(b<0)return -1;
        if(b==1)break; /* Occupied legacy slots are excluded, not rewritten. */
    }
    if(slot==FV_OTP_TOKEN_SLOTS || d->otp.can_program(d->otp.context,FV_OTP_MASK_PAGE) ||
       d->otp.can_program(d->otp.context,token_page(slot)))return -1;
    uint8_t secret[32]={0},verify[32]={0};int r=-1;
    if(mask_write(d,slot,FV_OTP_MASK_STARTED) || rng(context,secret,32) ||
       write_token(d,slot,secret) || read_token(d,slot,verify) || !fv_tag_equal(secret,verify))goto done;
    if(open_token(d,slot))goto done;
    for(unsigned bank=0;bank<2;bank++)if(d->flash.erase(d->flash.context,bank))goto done;
    if(flash_blank(d)!=1)goto done;
    fv_device_state next={.status=FV_ENROLLMENT_EMPTY,.sequence=1,.token_slot=slot,.policy=fv_auth_policy_default()};
    memcpy(next.device_id,d->device_id,16);
    if(fv_journal_commit(&d->journal,0,&next) || mask_write(d,slot,FV_OTP_MASK_READY))goto done;
    r=0;
done:
    mbedtls_platform_zeroize(secret,32);mbedtls_platform_zeroize(verify,32);
    if(r){d->opened=false;fv_journal_clear(&d->journal);}
    return r;
}
static int load(void *context,fv_device_state *s) {
    fv_enrollment_device *d=context;uint32_t state;
    memset(s,0,sizeof(*s));
    if(mask_read(d,d->token_slot,&state))return -1;
    if(d->terminal) {
        if(state!=FV_OTP_MASK_REVOKED)return -1;
        int b=burned(d,d->token_slot);if(b<0)return -1;
        /* Terminal projection, not an authenticated journal record. It carries
         * no vault reference or usable key; only cleanup/denial is possible. */
        s->status=b?FV_ENROLLMENT_DESTROYED:FV_ENROLLMENT_DESTROY_PENDING;
        s->token_slot=d->token_slot;s->credential_generation=1;
        s->policy=fv_auth_policy_default();memcpy(s->device_id,d->device_id,16);
        return 0;
    }
    if(!d->opened || state!=FV_OTP_MASK_READY || fv_journal_load(&d->journal,s) ||
       s->token_slot!=d->token_slot)return -1;
    return s->status==FV_ENROLLMENT_EMPTY || s->status==FV_ENROLLMENT_ACTIVE ||
           s->status==FV_ENROLLMENT_LOCKED || s->status==FV_ENROLLMENT_DESTROY_PENDING?0:-1;
}
static int commit(void *context,uint64_t previous,const fv_device_state *s) {
    fv_enrollment_device *d=context;uint32_t state;
    if(s->token_slot!=d->token_slot || mask_read(d,d->token_slot,&state))return -1;
    if(d->terminal) {
        /* Once revoked there is no journal key to persist. OTP is final. */
        return state==FV_OTP_MASK_REVOKED && s->status==FV_ENROLLMENT_DESTROYED &&
               burned(d,d->token_slot)==1?0:-1;
    }
    return d->opened && state==FV_OTP_MASK_READY?fv_journal_commit(&d->journal,previous,s):-1;
}
static int binding(void *context,const uint8_t id[16],uint32_t slot,uint8_t out[32]) {
    fv_enrollment_device *d=context;fv_device_state s;uint8_t token[32]={0};int r=-1;
    memset(out,0,32);
    if(load(d,&s) || s.token_slot!=slot ||
       (s.status!=FV_ENROLLMENT_EMPTY && s.status!=FV_ENROLLMENT_ACTIVE))goto done;
    if(s.status==FV_ENROLLMENT_ACTIVE && memcmp(id,s.volume_id,16))goto done;
    if(read_token(d,slot,token))goto done;
    /* No permanent root: the token is both the binding key and input material. */
    r=fv_vault_binding(token,token,slot,id,out);
done:mbedtls_platform_zeroize(token,32);return r;
}
static int destroy(void *context,uint32_t slot) {
    fv_enrollment_device *d=context;fv_device_state s;
    if(slot>=FV_OTP_TOKEN_SLOTS || load(d,&s) || s.token_slot!=slot ||
       s.status!=FV_ENROLLMENT_DESTROY_PENDING)return -1;
    if(mask_write(d,slot,FV_OTP_MASK_REVOKED))return -1;
    d->terminal=true;d->opened=false;fv_journal_clear(&d->journal);
    return finish_burn(d,slot);
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
