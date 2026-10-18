#include "fuse_vault/enrollment.h"
#include "fuse_vault/journal.h"
#include "fuse_vault/envelope.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
/* Mock media: OTP as 4096 rows of 24 bits, flash as 8 KiB. Models write-once
 * OTP (only 0->1 bit transitions) and flash program/erase. */
typedef struct {
    uint8_t flash[8192];uint32_t otp[4096];
    unsigned programs,erases,otp_writes,fail_program,fail_erase,fail_otp,partial,random;
    uint32_t otp_partial;
    bool denied;
} media;
static int read_flash(void *ctx,uint32_t offset,uint8_t *out,size_t n) {
    media *m=ctx;CHECK(offset+n<=8192);memcpy(out,m->flash+offset,n);return 0;
}
static int program(void *ctx,uint32_t offset,const uint8_t in[256]) {
    media *m=ctx;CHECK(offset%256==0 && offset<=8192-256);m->programs++;
    bool fail=m->programs==m->fail_program;unsigned n=fail?m->partial:256;
    for(unsigned i=0;i<n;i++){CHECK((m->flash[offset+i]&in[i])==in[i]);m->flash[offset+i]&=in[i];}
    return fail?-1:0;
}
static int erase(void *ctx,unsigned bank) {
    media *m=ctx;CHECK(bank<2);m->erases++;bool fail=m->erases==m->fail_erase;
    memset(m->flash+bank*4096,255,fail?m->partial:4096);return fail?-1:0;
}
static int raw_read(void *ctx,uint32_t row,uint32_t *out) {
    media *m=ctx;CHECK(row<4096);*out=m->otp[row];return m->denied?-1:0;
}
static int raw_write(void *ctx,uint32_t row,uint32_t value) {
    media *m=ctx;
    /* Page range: mask page 3, token pages 4-60. */
    unsigned page=row/64;CHECK(page==FV_OTP_MASK_PAGE || (page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57));
    CHECK(value<=0xffffff);CHECK(!(m->otp[row]&~value)); /* write-once: only set bits */
    m->otp_writes++;
    if(m->otp_writes==m->fail_otp){m->otp[row]|=value&m->otp_partial;return -1;}
    m->otp[row]|=value;return 0;
}
static int read_secret(void *ctx,unsigned row,uint8_t out[32]) {
    media *m=ctx;unsigned page=row/64;CHECK(page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57);CHECK(row%64+16<=64);if(m->denied)return -1;
    for(unsigned i=0;i<16;i++){uint32_t v=m->otp[row+i];out[2*i]=(uint8_t)v;out[2*i+1]=(uint8_t)(v>>8);}
    return 0;
}
static int write_secret(void *ctx,unsigned row,const uint8_t in[32]) {
    unsigned page=row/64;CHECK(page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57);CHECK(row%64+16<=64);
    for(unsigned i=0;i<16;i++)if(raw_write(ctx,row+i,0x10000u|in[2*i]|(uint32_t)in[2*i+1]<<8))return -1;
    return 0;
}
static int can_program(void *ctx,unsigned page){
    media *m=ctx;CHECK(page==FV_OTP_MASK_PAGE || (page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57));
    return m->denied?-1:0;
}
static int random_bytes(void *ctx,uint8_t *out,size_t n){media *m=ctx;for(size_t i=0;i<n;i++)out[i]=(uint8_t)++m->random;return 0;}
static const uint8_t id[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static void init_media(media *m){memset(m,0,sizeof(*m));memset(m->flash,255,8192);memset(m->otp,0,sizeof(m->otp));m->otp_partial=0xff;}
static fv_journal_io flash_io(media *m){return (fv_journal_io){m,read_flash,program,erase};}
/* Read a token's 3-bit mask state directly from page 3 OTP. */
static uint32_t mask_state(media *m,unsigned slot){
    unsigned row=slot/FV_OTP_SLOTS_PER_MASK_ROW,bit=(slot%FV_OTP_SLOTS_PER_MASK_ROW)*3;
    return (m->otp[FV_OTP_MASK_PAGE*64+row]>>bit)&0x7u;
}
/* Write a token's 3-bit mask state directly to page 3 OTP (write-once). */
static int mask_write_test(fv_enrollment_device *d,unsigned slot,uint32_t state){
    uint32_t row=slot/FV_OTP_SLOTS_PER_MASK_ROW,bit=(slot%FV_OTP_SLOTS_PER_MASK_ROW)*3;
    uint32_t v;
    if(d->otp.read_raw(d->otp.context,FV_OTP_MASK_PAGE*64+row,&v))return -1;
    uint32_t current=(v>>bit)&0x7u;
    if(current & ~state)return -1; /* cannot clear bits */
    uint32_t newv=(v & ~(0x7u<<bit)) | (state<<bit);
    if(d->otp.write_raw(d->otp.context,FV_OTP_MASK_PAGE*64+row,newv))return -1;
    return 0;
}
/* Read a token's secret from its 16 ECC rows. */
static void read_token_secret(media *m,unsigned slot,uint8_t out[32]){
    unsigned page=FV_OTP_TOKEN_PAGE+slot/4,base=(slot%4)*16;
    for(unsigned i=0;i<16;i++){uint32_t v=m->otp[page*64+base+i];out[2*i]=(uint8_t)v;out[2*i+1]=(uint8_t)(v>>8);}
}
static void init_device(fv_enrollment_device *d,media *m) {
    fv_enrollment_init(d,(fv_otp_io){m,raw_read,raw_write,read_secret,write_secret,can_program},flash_io(m),id);
}
static void test_mask_transitions(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    /* Slot 0 starts UNUSED */
    CHECK(mask_state(&m,0)==FV_OTP_MASK_UNUSED);
    /* UNUSED -> STARTED */
    CHECK(!mask_write_test(&d,0,FV_OTP_MASK_STARTED));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_STARTED);
    /* STARTED -> READY */
    CHECK(!mask_write_test(&d,0,FV_OTP_MASK_READY));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_READY);
    /* READY -> REVOKED */
    CHECK(!mask_write_test(&d,0,FV_OTP_MASK_REVOKED));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    /* Cannot go back: REVOKED -> READY should fail (clears bits) */
    CHECK(mask_write_test(&d,0,FV_OTP_MASK_READY)==-1);
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    /* STARTED -> UNUSED (clearing) must fail */
    init_media(&m);init_device(&d,&m);
    CHECK(!mask_write_test(&d,0,FV_OTP_MASK_STARTED));
    CHECK(mask_write_test(&d,0,FV_OTP_MASK_UNUSED)==-1);
    CHECK(mask_state(&m,0)==FV_OTP_MASK_STARTED);
    puts("mask transitions: UNUSED/STARTED/READY/REVOKED, write-once enforced");
}
static void test_provision_first(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(fv_enrollment_open(&d)==1); /* virgin */
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(fv_enrollment_open(&d)==0); /* opened */
    /* Slot 0 should be READY in the mask */
    CHECK(mask_state(&m,0)==FV_OTP_MASK_READY);
    /* Token secret should be non-blank */
    CHECK(fv_enrollment_token_blank(&d,0)==0);
    /* Other slots should be UNUSED */
    CHECK(mask_state(&m,1)==FV_OTP_MASK_UNUSED);
    /* Journal state */
    fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
    CHECK(!a.load(a.context,&s));
    CHECK(s.status==FV_ENROLLMENT_EMPTY && s.sequence==1 && s.token_slot==0);
    puts("provision first: virgin->opened, slot 0 READY, journal EMPTY seq 1");
}
static void test_binding_from_token_only(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    fv_device_authority a=fv_enrollment_authority(&d);
    uint8_t b1[32],b2[32];uint8_t volume[16]={99};
    CHECK(!a.binding(a.context,volume,0,b1));
    /* Same volume + slot => same binding */
    CHECK(!a.binding(a.context,volume,0,b2));
    CHECK(!memcmp(b1,b2,32));
    /* Different volume => different binding */
    uint8_t volume2[16]={100};CHECK(!a.binding(a.context,volume2,0,b2));
    CHECK(memcmp(b1,b2,32));
    /* Token secret should be non-blank (provisioning wrote it) */
    uint8_t token[32];read_token_secret(&m,0,token);
    int nonzero=0;for(unsigned i=0;i<32;i++)if(token[i])nonzero=1;CHECK(nonzero);
    puts("binding from token only: same inputs same output, different volume differs");
}
static void test_destroy_and_rotate(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    fv_device_authority a=fv_enrollment_authority(&d);
    fv_device_state s;
    CHECK(!a.load(a.context,&s));CHECK(s.token_slot==0);
    s.status=FV_ENROLLMENT_ACTIVE;s.credential_generation=1;s.volume_id[0]=99;s.header_hash[0]=55;s.sequence++;
    CHECK(!a.commit(a.context,1,&s));
    /* Request destruction with fail injection on the 4th OTP write: mask write
     * succeeds, token burn is interrupted. Destruction must fail. */
    m.fail_otp=m.otp_writes+4;CHECK(fv_enrollment_request_destruction(&d));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    /* Binding fails once destruction has started (journal says DESTROY_PENDING). */
    uint8_t volume[16]={99},b2[32];
    CHECK(a.binding(a.context,volume,0,b2)!=0);m.fail_otp=0;
    /* Retry destruction: should now succeed cleanly. */
    CHECK(!fv_enrollment_request_destruction(&d));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    for(unsigned i=0;i<16;i++)CHECK(m.otp[FV_OTP_TOKEN_PAGE*64+i]==0xffffff);
    CHECK(!a.load(a.context,&s));
    CHECK(s.status==FV_ENROLLMENT_DESTROYED);
    /* Provision next token (slot 1) */
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(!a.load(a.context,&s));
    CHECK(s.token_slot==1 && s.status==FV_ENROLLMENT_EMPTY);
    CHECK(mask_state(&m,1)==FV_OTP_MASK_READY);
    /* Slot 0 binding fails (revoked), slot 1 works */
    CHECK(a.binding(a.context,volume,0,b2)!=0);
    CHECK(!a.binding(a.context,volume,1,b2));
    puts("destroy and rotate: slot 0 revoked+burned, slot 1 provisioned, journal advanced");
}
static void test_interrupted_provisioning(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    /* Manually mark slot 0 STARTED without completing (simulates interruption) */
    CHECK(!mask_write_test(&d,0,FV_OTP_MASK_STARTED));
    /* Token rows still blank */
    CHECK(fv_enrollment_token_blank(&d,0)==1);
    /* Next provision should consume slot 0 (REVOKED) and use slot 1 */
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    CHECK(mask_state(&m,1)==FV_OTP_MASK_READY);
    fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
    CHECK(!a.load(a.context,&s));CHECK(s.token_slot==1);
    puts("interrupted provisioning: STARTED slot consumed, next slot used");
}
static void test_228_slot_addressing(void) {
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        unsigned page=FV_OTP_TOKEN_PAGE+slot/4;
        unsigned base=(slot%4)*16;
        CHECK(page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57);
        CHECK(base%16==0 && base<64);
    }
    CHECK(4+227/4==60);
    CHECK((227%4)*16==48);
    CHECK(FV_OTP_MASK_ROWS*FV_OTP_SLOTS_PER_MASK_ROW>=FV_OTP_TOKEN_SLOTS);
    puts("228 slot addressing: pages 4-60, mask page 3, 29 rows");
}
static void test_journal_key_from_token(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    uint8_t token[32];read_token_secret(&m,0,token);
    uint8_t key1[32],key2[32];
    CHECK(!fv_journal_key(token,id,0,key1));
    CHECK(!fv_journal_key(token,id,0,key2));
    /* Independent Python hashlib/hmac HKDF vector, including NUL and LE slot. */
    static const uint8_t expected[32]={0x21,0xbf,0x55,0xca,0xbc,0x4d,0x6f,0xd9,0xef,0xd5,0x65,0x31,0xad,0x3c,0x16,0xc2,0x76,0xe0,0xad,0x88,0x5c,0xd1,0x66,0x7a,0xd7,0xf7,0xf0,0x3c,0x20,0x11,0x4b,0x62};
    CHECK(!memcmp(key1,expected,32));
    CHECK(!memcmp(key1,key2,32));CHECK(!memcmp(key1,d.journal.key,32));
    CHECK(!fv_journal_key(token,id,1,key2));CHECK(memcmp(key1,key2,32));
    uint8_t other_id[16];memcpy(other_id,id,16);other_id[0]^=1;
    CHECK(!fv_journal_key(token,other_id,0,key2));CHECK(memcmp(key1,key2,32));
    uint8_t volume[16]={99},binding[32];fv_device_authority a=fv_enrollment_authority(&d);
    CHECK(!a.binding(a.context,volume,0,binding));CHECK(memcmp(key1,binding,32));
    int nonzero=0;for(unsigned i=0;i<32;i++)if(key1[i])nonzero=1;CHECK(nonzero);
    /* Different token => different key */
    uint8_t token2[32];memcpy(token2,token,32);token2[0]^=1;
    uint8_t key3[32];CHECK(!fv_journal_key(token2,id,0,key3));
    CHECK(memcmp(key1,key3,32));
    puts("journal key from token: deterministic, token-dependent, no root");
}
static void test_denied_otp(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    m.denied=true;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==-1);
    CHECK(m.otp_writes==0);
    puts("denied OTP: provisioning refused, no writes");
}
static void activate(fv_enrollment_device *d) {
    fv_device_authority a=fv_enrollment_authority(d);fv_device_state s;
    CHECK(!a.load(a.context,&s));uint64_t previous=s.sequence;
    s.status=FV_ENROLLMENT_ACTIVE;s.sequence++;s.credential_generation=1;
    s.volume_id[0]=99;s.header_hash[0]=55;
    CHECK(!a.commit(a.context,previous,&s));
}
static void check_revoked_burned(media *m) {
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        if(mask_state(m,slot)!=FV_OTP_MASK_REVOKED)continue;
        unsigned row=(FV_OTP_TOKEN_PAGE+slot/4)*64+(slot%4)*16;
        for(unsigned i=0;i<16;i++)CHECK(m->otp[row+i]==0xffffff);
    }
}
static void test_all_slots(void) {
    media m;fv_enrollment_device d;init_media(&m);
    uint8_t old_key[32]={0};
    for(unsigned slot=0;slot<FV_OTP_TOKEN_SLOTS;slot++) {
        init_device(&d,&m);CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
        fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
        CHECK(!a.load(a.context,&s));CHECK(s.token_slot==slot && s.sequence==1 && !s.attempts);
        CHECK(memcmp(old_key,d.journal.key,32));memcpy(old_key,d.journal.key,32);
        activate(&d);CHECK(!fv_enrollment_request_destruction(&d));
        init_device(&d,&m);CHECK(!fv_enrollment_open(&d));CHECK(!a.load(a.context,&s));
        CHECK(s.status==FV_ENROLLMENT_DESTROYED);check_revoked_burned(&m);
    }
    unsigned writes=m.otp_writes,programs=m.programs,erases=m.erases;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(writes==m.otp_writes && programs==m.programs && erases==m.erases);
}
static void test_provision_cuts(void) {
    /* Every token/mask write; flash byte cuts on body and commit; both erases.
     * Reinitialize all RAM before retrying, including the journal key. */
    for(unsigned kind=0;kind<3;kind++) {
        unsigned operations=kind==0?18:2;
        unsigned max_cut=kind==0?1:kind==1?256:4096;
        unsigned step=kind==2?127:1;
        for(unsigned op=1;op<=operations;op++)for(unsigned cut=0;cut<=max_cut;cut+=step) {
            media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
            if(kind==0){m.fail_otp=op;m.otp_partial=cut?0xffffff:0;}
            if(kind==1){m.fail_program=op;m.partial=cut;}
            if(kind==2){memset(m.flash,0,sizeof(m.flash));m.fail_erase=op;m.partial=cut;}
            CHECK(fv_enrollment_provision(&d,random_bytes,&m));
            m.fail_otp=m.fail_program=m.fail_erase=0;init_device(&d,&m);
            CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
            check_revoked_burned(&m);
            fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
            CHECK(!a.load(a.context,&s));CHECK(s.status==FV_ENROLLMENT_EMPTY && s.sequence==1);
            CHECK(mask_state(&m,s.token_slot)==FV_OTP_MASK_READY);
        }
    }
}
static void test_destruction_cuts(void) {
    for(unsigned op=1;op<=17;op++)for(unsigned persisted=0;persisted<=1;persisted++) {
        media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
        CHECK(!fv_enrollment_provision(&d,random_bytes,&m));activate(&d);
        m.fail_otp=m.otp_writes+op;m.otp_partial=persisted?0xffffff:0;
        CHECK(fv_enrollment_request_destruction(&d));m.fail_otp=0;
        init_device(&d,&m);CHECK(!fv_enrollment_open(&d));
        CHECK(!fv_enrollment_request_destruction(&d));check_revoked_burned(&m);
        CHECK(!fv_enrollment_provision(&d,random_bytes,&m));check_revoked_burned(&m);
    }
    /* A committed intent with an untouched READY mask must recover at boot. */
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));activate(&d);
    fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;CHECK(!a.load(a.context,&s));
    s.sequence++;s.status=FV_ENROLLMENT_DESTROY_PENDING;CHECK(!a.commit(a.context,s.sequence-1,&s));
    init_device(&d,&m);CHECK(!fv_enrollment_open(&d));
    fv_vault_platform p={.authority=a,.kdf_limits={1,100}};
    CHECK(fv_vault_recover(&p)==FV_VAULT_DENIED);check_revoked_burned(&m);
}
static void test_abandoned_cleanup_cuts(void) {
    for(unsigned op=1;op<=18;op++)for(unsigned persisted=0;persisted<=1;persisted++) {
        media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
        CHECK(!mask_write_test(&d,0,FV_OTP_MASK_STARTED));
        for(unsigned i=0;i<16;i++)m.otp[FV_OTP_TOKEN_PAGE*64+i]=0x11234;
        m.fail_otp=m.otp_writes+op;m.otp_partial=persisted?0xffffff:0;
        CHECK(fv_enrollment_provision(&d,random_bytes,&m));m.fail_otp=0;
        init_device(&d,&m);CHECK(!fv_enrollment_provision(&d,random_bytes,&m));check_revoked_burned(&m);
        CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED && mask_state(&m,1)==FV_OTP_MASK_READY);
    }
}
static void test_legacy_and_ambiguous_otp(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    /* ECC-only bits are occupied even if the decoded payload looks zero. */
    m.otp[FV_OTP_TOKEN_PAGE*64]=0x10000;
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));CHECK(mask_state(&m,1)==FV_OTP_MASK_READY);
    CHECK(m.otp[FV_OTP_TOKEN_PAGE*64]==0x10000);
    CHECK(!mask_write_test(&d,2,FV_OTP_MASK_READY));
    unsigned writes=m.otp_writes,programs=m.programs,erases=m.erases;
    CHECK(fv_enrollment_open(&d)<0);CHECK(fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(writes==m.otp_writes && programs==m.programs && erases==m.erases);
    for(unsigned state=1;state<=3;state++) {
        init_media(&m);init_device(&d,&m);m.otp[FV_OTP_MASK_PAGE*64]=state;
        CHECK(fv_enrollment_open(&d)<0);CHECK(fv_enrollment_provision(&d,random_bytes,&m));
        CHECK(!m.otp_writes && !m.programs && !m.erases);
    }
}
static void test_journal_rotation_and_loss(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));activate(&d);
    uint8_t old_flash[8192],old_key[32];memcpy(old_flash,m.flash,8192);memcpy(old_key,d.journal.key,32);
    CHECK(!fv_enrollment_request_destruction(&d));init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));CHECK(memcmp(old_key,d.journal.key,32));
    memcpy(m.flash,old_flash,8192);init_device(&d,&m);
    CHECK(fv_enrollment_open(&d)<0);
    unsigned writes=m.otp_writes,programs=m.programs,erases=m.erases;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(writes==m.otp_writes && programs==m.programs && erases==m.erases);
    memset(m.flash,255,8192);init_device(&d,&m);CHECK(fv_enrollment_open(&d)<0);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));
    CHECK(writes==m.otp_writes && programs==m.programs && erases==m.erases);
}

static void test_stored_lockout_policy(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));activate(&d);
    fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
    CHECK(!a.load(a.context,&s));s.policy.limit_action=FV_LIMIT_LOCKOUT;
    s.attempts=s.policy.max_attempts;s.sequence++;
    CHECK(!a.commit(a.context,s.sequence-1,&s));
    init_device(&d,&m);CHECK(!fv_enrollment_open(&d));
    fv_vault_platform p={.authority=a,.kdf_limits={1,100}};
    CHECK(fv_vault_recover(&p)==FV_VAULT_DENIED);
    init_device(&d,&m);CHECK(!fv_enrollment_open(&d));CHECK(!a.load(a.context,&s));
    CHECK(s.status==FV_ENROLLMENT_LOCKED && s.policy.limit_action==FV_LIMIT_LOCKOUT);
    CHECK(mask_state(&m,0)==FV_OTP_MASK_READY);CHECK(fv_enrollment_provision(&d,random_bytes,&m));
}
int main(void){
    test_stored_lockout_policy();
    test_all_slots();
    test_provision_cuts();
    test_destruction_cuts();
    test_abandoned_cleanup_cuts();
    test_legacy_and_ambiguous_otp();
    test_journal_rotation_and_loss();
    test_mask_transitions();
    test_provision_first();
    test_binding_from_token_only();
    test_destroy_and_rotate();
    test_interrupted_provisioning();
    test_228_slot_addressing();
    test_journal_key_from_token();
    test_denied_otp();
    puts("OTP mask/token: all tests passed");
    return 0;
}
