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
/* Token rows for a slot: 16 ECC rows holding the 32-byte secret, read/written
 * through the ECC-protected secret interface at the slot's row offset. */
static int token_blank(fv_enrollment_device *d,unsigned slot) {
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    uint8_t secret[32]={0};
    int r=d->otp.read_secret(d->otp.context,row,secret);
    int blank=1;
    if(r<0)return -1;
    for(unsigned i=0;i<32;i++)if(secret[i])blank=0;
    mbedtls_platform_zeroize(secret,32);
    return blank;
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
    if(read_row(d,FV_OTP_MASK_PAGE,mask_row(slot),&v))return -1;
    *state=(v>>mask_bit(slot))&0x7u;
    return 0;
}
static int mask_write(fv_enrollment_device *d,unsigned slot,uint32_t state) {
    uint32_t v;
    if(read_row(d,FV_OTP_MASK_PAGE,mask_row(slot),&v))return -1;
    uint32_t current=(v>>mask_bit(slot))&0x7u;
    if(current & ~state)return -1; /* OTP write-once: cannot clear bits */
    uint32_t newv=(v & ~(0x7u<<mask_bit(slot))) | (state<<mask_bit(slot));
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
/* Write a token's 32-byte secret via the ECC-protected interface at the slot's
 * row offset within its page. All 64 rows on the token page must be blank
 * before any secret is written there (ECC programming is all-or-nothing per
 * 32-byte block). */
static int write_token(fv_enrollment_device *d,unsigned slot,const uint8_t in[32]) {
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    /* Verify the slot's 16 rows are blank before programming its ECC block. */
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++) {
        uint32_t raw;
        if(read_row(d,token_page(slot),token_row_offset(slot)+i,&raw))return -1;
        if(raw)return -1;
    }
    return d->otp.write_secret(d->otp.context,row,in);
}
void fv_enrollment_init(fv_enrollment_device *d,fv_otp_io otp,fv_journal_io flash,const uint8_t id[16]) {
    memset(d,0,sizeof(*d));d->otp=otp;d->flash=flash;memcpy(d->device_id,id,16);
}
void fv_enrollment_clear(fv_enrollment_device *d){mbedtls_platform_zeroize(d,sizeof(*d));}
/* Find the current (READY) token in the mask, read its secret, and open the
 * journal with a token-derived key. No permanent device root is involved. */
static int open_current(fv_enrollment_device *d) {
    uint32_t state;uint8_t token[32]={0},key[32]={0};int r=0;
    d->opened=false;fv_journal_clear(&d->journal);
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        if(mask_read(d,slot,&state)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
        if(state==FV_OTP_MASK_READY) {
            /* Journal key is derived from the token secret, not device_id.
             * This ensures a fresh journal per enrollment and that records
             * cannot authenticate under a different token. */
            if(read_token(d,slot,token)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            if(fv_journal_key(token,d->device_id,key)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            fv_journal_init(&d->journal,d->flash,key,d->device_id);
            d->opened=true;mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return 0;
        }
    }
    mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);
    /* No READY token: try each REVOKED token's secret to load the journal.
     * The journal was created with the token-derived key, so device_id won't
     * work for intact tokens. After destruction the token is burned, so fall
     * back to a device-bound key to read the final DESTROYED state. */
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        if(mask_read(d,slot,&state)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
        if(state!=FV_OTP_MASK_REVOKED)continue;
        if(read_token(d,slot,token)==0) {
            if(fv_journal_key(token,d->device_id,key)==0) {
                fv_journal_init(&d->journal,d->flash,key,d->device_id);
                fv_device_state s;
                if(fv_journal_load(&d->journal,&s)==FV_JOURNAL_OK) {
                    d->opened=true;mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return 0;
                }
            }
            mbedtls_platform_zeroize(token,32);
        }
        /* Fallback: device-bound key for reading destroyed state after burn. */
        {
            uint8_t ikm[32]={0};memcpy(ikm,d->device_id,16);
            if(fv_journal_key(ikm,d->device_id,key)==0) {
                fv_journal_init(&d->journal,d->flash,key,d->device_id);
                fv_device_state s;
                if(fv_journal_load(&d->journal,&s)==FV_JOURNAL_OK) {
                    d->opened=true;mbedtls_platform_zeroize(key,32);mbedtls_platform_zeroize(ikm,32);return 0;
                }
            }
            mbedtls_platform_zeroize(key,32);mbedtls_platform_zeroize(ikm,32);
        }
    }
    return r;
}
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
    int r=open_current(d);
    if(r<0)return r;
    if(!d->opened) {
        /* No READY token. Virgin if everything is blank; inconsistent otherwise. */
        if(blank(d,FV_OTP_MASK_PAGE)!=1)return -1;
        for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++)
            if(token_blank(d,slot)!=1)return -1;
        if(flash_blank(d)!=1)return -1;
        return 1;
    }
    fv_device_state s;int result=fv_journal_load(&d->journal,&s);
    if(result==FV_JOURNAL_EMPTY)return -1; /* token exists but journal missing: inconsistent */
    return result;
}
/* Provision one token: mark STARTED, write secret, verify, mark READY. */
static int provision_token(fv_enrollment_device *d,unsigned slot,fv_random_bytes rng,void *context) {
    uint32_t state;uint8_t secret[32]={0},verify[32]={0};int r=-1;
    if(mask_read(d,slot,&state) || state!=FV_OTP_MASK_UNUSED)goto done;
    if(token_blank(d,slot)!=1)goto done;
    if(mask_write(d,slot,FV_OTP_MASK_STARTED))goto done;
    if(rng(context,secret,32))goto done;
    if(write_token(d,slot,secret))goto done;
    if(read_token(d,slot,verify) || !fv_tag_equal(secret,verify))goto done;
    if(mask_write(d,slot,FV_OTP_MASK_READY))goto done;
    r=0;
done:mbedtls_platform_zeroize(secret,32);mbedtls_platform_zeroize(verify,32);return r;
}
/* Finish burning a REVOKED slot: program all 16 token rows to 0xffffff. Returns
 * 0 when all rows are 0xffffff (burn complete), -1 on I/O failure. */
static int finish_burn(fv_enrollment_device *d,unsigned slot) {
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++) {
        if(write_row(d,token_page(slot),token_row_offset(slot)+i,0xffffff))return -1;
    }
    return 0;
}
int fv_enrollment_provision(fv_enrollment_device *d,fv_random_bytes rng,void *context) {
    if(!rng)return -1;
    uint8_t token[32]={0},key[32]={0};
    uint64_t next_seq = 1;
    uint64_t prev_seq = 0;
    int open_result = fv_enrollment_open(d);
    /* If a journal is already open (READY token found), inspect its state. */
    if(d->opened) {
        fv_device_state old={0};
        int jr=fv_journal_load(&d->journal,&old);
        if(jr==FV_JOURNAL_OK) {
            if(old.status==FV_ENROLLMENT_EMPTY){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return 0;}
            if(old.status==FV_ENROLLMENT_DESTROYED){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);next_seq=old.sequence+1;prev_seq=old.sequence;goto find_next_slot;}
            mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1; /* ACTIVE or other: cannot provision */
        }
        /* Journal corrupt or empty: inconsistent state, deny access. */
        mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;
    }
    /* No open journal: must be virgin (everything blank) or have interrupted
     * STARTED/REVOKED slots to clean up before finding the next UNUSED slot. */
    if(open_result==1) {
        /* Virgin: nothing to consume, go straight to find_next_slot. */
    } else if(open_result==-1) {
        /* Inconsistent state: consume any STARTED slots and finish burning any
         * REVOKED slots whose burn was interrupted. Also read the journal to
         * get the previous sequence number for the next enrollment. */
        uint32_t state;
        for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
            if(mask_read(d,slot,&state)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            if(state==FV_OTP_MASK_STARTED) {
                if(mask_write(d,slot,FV_OTP_MASK_REVOKED)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            }
            if(state==FV_OTP_MASK_REVOKED) {
                if(finish_burn(d,slot)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            }
        }
        /* Try to read the journal for the previous sequence number. */
        {
            uint8_t ikm[32]={0};memcpy(ikm,d->device_id,16);
            if(!fv_journal_key(ikm,d->device_id,key)) {
                fv_journal_init(&d->journal,d->flash,key,d->device_id);
                fv_device_state old;
                if(fv_journal_load(&d->journal,&old)==FV_JOURNAL_OK) {
                    next_seq=old.sequence+1;
                    prev_seq=old.sequence;
                }
            }
            mbedtls_platform_zeroize(key,32);mbedtls_platform_zeroize(ikm,32);
        }
    } else {
        mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;
    }
find_next_slot:
    /* Find first UNUSED slot with blank token rows. Slots with programmed rows
     * (from previous enrollments on development units) are skipped. */
    uint32_t state;
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        if(mask_read(d,slot,&state)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
        if(state==FV_OTP_MASK_UNUSED) {
            if(token_blank(d,slot)!=1)continue; /* skip previously programmed slot */
            if(provision_token(d,slot,rng,context)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            /* Journal key is token-derived, not device-bound. */
            if(read_token(d,slot,token)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);goto fail;}
            if(fv_journal_key(token,d->device_id,key)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);goto fail;}
            fv_journal_init(&d->journal,d->flash,key,d->device_id);
            d->opened=true;
            mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);
            fv_device_state next={.status=FV_ENROLLMENT_EMPTY,.sequence=next_seq,.token_slot=slot,.policy=fv_auth_policy_default()};
            memcpy(next.device_id,d->device_id,16);
            if(fv_journal_commit(&d->journal,prev_seq,&next)){mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;}
            return 0;
        }
    }
    mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);
    return -1; /* no slots available */
fail:mbedtls_platform_zeroize(token,32);mbedtls_platform_zeroize(key,32);return -1;
}
static int load(void *context,fv_device_state *s) {
    fv_enrollment_device *d=context;
    if(!d->opened || fv_journal_load(&d->journal,s))return -1;
    uint32_t state;
    if(mask_read(d,s->token_slot,&state))return -1;
    int destroyed = s->status==FV_ENROLLMENT_DESTROY_PENDING || s->status==FV_ENROLLMENT_DESTROYED;
    if(destroyed) {
        /* DESTROY_PENDING accepts READY (interrupted before revocation) or
         * REVOKED (revocation in progress/complete). DESTROYED requires
         * REVOKED (burn complete). */
        if(s->status==FV_ENROLLMENT_DESTROYED && state!=FV_OTP_MASK_REVOKED)return -1;
    } else {
        if(state!=FV_OTP_MASK_READY)return -1;
    }
    if(s->status!=FV_ENROLLMENT_EMPTY && s->status!=FV_ENROLLMENT_ACTIVE &&
       s->status!=FV_ENROLLMENT_DESTROY_PENDING && s->status!=FV_ENROLLMENT_DESTROYED)return -1;
    return 0;
}
static int commit(void *context,uint64_t previous,const fv_device_state *s) {
    fv_enrollment_device *d=context;
    return d->opened?fv_journal_commit(&d->journal,previous,s):-1;
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
    if(!d->opened || fv_journal_load(&d->journal,&s) || s.token_slot!=slot ||
       s.status!=FV_ENROLLMENT_DESTROY_PENDING || slot>=FV_OTP_TOKEN_SLOTS)return -1;
    /* Write REVOKED first (write-once, can't go back), then burn the token. */
    if(mask_write(d,slot,FV_OTP_MASK_REVOKED))return -1;
    for(unsigned i=0;i<FV_OTP_SECRET_ROWS;i++)
        if(write_row(d,token_page(slot),token_row_offset(slot)+i,0xffffff))return -1;
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
