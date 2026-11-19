#include "fuse_vault/enrollment.h"
#include "fuse_vault/journal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
/* Mock media: OTP as 4096 rows of 24 bits, flash as 8 KiB in two banks.
 * Models write-once OTP (only 0->1 bit transitions) and flash program/erase. */
typedef struct {
    uint8_t flash[8192];uint32_t otp[4096];
    unsigned programs,erases,otp_writes,fail_program,fail_erase,fail_otp,fail_read,partial;
    uint32_t otp_partial,random;bool denied;
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
    media *m=ctx;CHECK(row<4096);*out=m->otp[row];return m->fail_read||m->denied?-1:0;
}
static int raw_write(void *ctx,uint32_t row,uint32_t value) {
    media *m=ctx;unsigned page=row/64;
    CHECK(page==FV_OTP_MASK_PAGE || (page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57));
    CHECK(value<=0xffffff);CHECK(!(m->otp[row]&~value)); /* write-once: only set bits */
    m->otp_writes++;
    if(m->otp_writes==m->fail_otp){m->otp[row]|=value&m->otp_partial;return -1;}
    m->otp[row]|=value;return 0;
}
static int read_secret(void *ctx,unsigned row,uint8_t out[32]) {
    media *m=ctx;unsigned page=row/64;
    CHECK(page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57);
    CHECK(row%64+16<=64);if(m->fail_read||m->denied)return -1;
    for(unsigned i=0;i<16;i++){uint32_t v=m->otp[row+i];out[2*i]=(uint8_t)v;out[2*i+1]=(uint8_t)(v>>8);}
    return 0;
}
static int write_secret(void *ctx,unsigned row,const uint8_t in[32]) {
    unsigned page=row/64;CHECK(page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57);
    CHECK(row%64+16<=64);
    for(unsigned i=0;i<16;i++)if(raw_write(ctx,row+i,0x10000u|in[2*i]|(uint32_t)in[2*i+1]<<8))return -1;
    return 0;
}
static int can_program(void *ctx,unsigned page) {
    media *m=ctx;CHECK(page==FV_OTP_MASK_PAGE || (page>=FV_OTP_TOKEN_PAGE && page<FV_OTP_TOKEN_PAGE+57));
    return m->denied?-1:0;
}
static int random_bytes(void *ctx,uint8_t *out,size_t n){media *m=ctx;for(size_t i=0;i<n;i++)out[i]=(uint8_t)++m->random;return 0;}
static const uint8_t id[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static void init_media(media *m){memset(m,0,sizeof(*m));memset(m->flash,255,8192);memset(m->otp,0,sizeof(m->otp));m->otp_partial=0xff;}
static fv_otp_io otp_io(media *m){return (fv_otp_io){m,raw_read,raw_write,read_secret,write_secret,can_program};}
static fv_journal_io flash_io(media *m){return (fv_journal_io){m,read_flash,program,erase};}
static uint32_t mask_row(unsigned slot){return slot/FV_OTP_SLOTS_PER_MASK_ROW;}
static uint32_t mask_bit(unsigned slot){return (slot%FV_OTP_SLOTS_PER_MASK_ROW)*3u;}
static unsigned token_page(unsigned slot){return FV_OTP_TOKEN_PAGE+slot/FV_OTP_TOKENS_PER_PAGE;}
static unsigned token_row_offset(unsigned slot){return (slot%FV_OTP_TOKENS_PER_PAGE)*FV_OTP_TOKEN_ROW_STRIDE;}
/* Read a slot's 3-bit mask state directly from page 3 OTP. */
static uint32_t mask_state(media *m,unsigned slot) {
    uint32_t v;CHECK(!raw_read(m,FV_OTP_MASK_PAGE*64+mask_row(slot),&v));
    return (v>>mask_bit(slot))&0x7u;
}
/* Directly plant a mask state (bypasses write-once): fixture setup only. */
static void plant_mask(media *m,unsigned slot,uint32_t state) {
    unsigned row=FV_OTP_MASK_PAGE*64+mask_row(slot);uint32_t v=m->otp[row];
    v=(v & ~(0x7u<<mask_bit(slot))) | (state<<mask_bit(slot));
    m->otp[row]=v;
}
/* Directly occupy a token's 16 ECC rows (fixture setup only). */
static void occupy_token(media *m,unsigned slot) {
    uint8_t secret[32];memset(secret,0xaa,sizeof(secret));
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    CHECK(!write_secret(m,row,secret));
}
/* All 16 raw rows of a token must be 0xffffff (fully burned). */
static int token_fully_burned(media *m,unsigned slot) {
    unsigned row=token_page(slot)*64+token_row_offset(slot);
    for(unsigned i=0;i<16;i++) if(m->otp[row+i]!=0xffffff) return 0;
    return 1;
}
static fv_enrollment_device open_device(media *m) {
    fv_enrollment_device d;fv_enrollment_init(&d,otp_io(m),flash_io(m),id);return d;
}
static int load_state(fv_enrollment_device *d,fv_device_state *s) {
    fv_device_authority a=fv_enrollment_authority(d);return a.load(a.context,s);
}
static int commit_state(fv_enrollment_device *d,uint64_t previous,const fv_device_state *s) {
    fv_device_authority a=fv_enrollment_authority(d);return a.commit(a.context,previous,s);
}
/* Transition the loaded EMPTY journal to a valid ACTIVE state, as creation would. */
static void activate(fv_enrollment_device *d) {
    fv_device_state s;CHECK(!load_state(d,&s));
    s.status=FV_ENROLLMENT_ACTIVE;s.credential_generation=1;s.sequence=2;
    memset(s.volume_id,1,16);memset(s.header_hash,2,32);
    s.attempts=0;s.attempt_pending=false;s.token_slot=0;
    CHECK(!commit_state(d,1,&s));
}
/* Fresh device is virgin: open reports 1 and everything is blank. */
static void virgin(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_open(&d)==1);
    fv_enrollment_inventory inv=fv_enrollment_inspect(&d);
    CHECK(inv.mask_blank==1 && inv.tokens_blank==1 && inv.flash_blank==1);
    CHECK(fv_enrollment_token_blank(&d,0)==1);
    CHECK(fv_enrollment_token_blank(&d,227)==1);
    fv_enrollment_clear(&d);
}
/* Provisioning a virgin device creates one READY token, an EMPTY journal and
 * opens as the current enrollment. */
static void provision_creates_ready(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(fv_enrollment_open(&d)==0);
    CHECK(mask_state(&m,0)==FV_OTP_MASK_READY);
    CHECK(fv_enrollment_token_blank(&d,0)==0);
    fv_device_state s;CHECK(!load_state(&d,&s));
    CHECK(s.status==FV_ENROLLMENT_EMPTY && s.sequence==1 && s.token_slot==0);
    CHECK(s.attempts==0 && !s.attempt_pending && s.credential_generation==0);
    fv_enrollment_clear(&d);
}
/* Re-provisioning an enrolled device is a no-op: same READY token, no new slot. */
static void reprovision_is_noop(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    unsigned first_slot=0;CHECK(mask_state(&m,first_slot)==FV_OTP_MASK_READY);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(mask_state(&m,first_slot)==FV_OTP_MASK_READY);
    CHECK(fv_enrollment_open(&d)==0);
    fv_device_state s;CHECK(!load_state(&d,&s));CHECK(s.token_slot==first_slot);
    fv_enrollment_clear(&d);
}
/* An abandoned STARTED provisioning is revoked and burned, never exposed as
 * READY; re-provisioning then allocates the next blank token. */
static void abandoned_started_recovered(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    /* Plant a started token at slot 5 without ever marking it READY. */
    occupy_token(&m,5);plant_mask(&m,5,FV_OTP_MASK_STARTED);
    CHECK(fv_enrollment_open(&d)==2); /* explicit setup required, not READY */
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(mask_state(&m,5)==FV_OTP_MASK_REVOKED);
    CHECK(token_fully_burned(&m,5)); /* every raw row burned to 0xffffff */
    CHECK(mask_state(&m,0)==FV_OTP_MASK_READY);
    CHECK(fv_enrollment_open(&d)==0);
    fv_device_state s;CHECK(!load_state(&d,&s));CHECK(s.token_slot==0);
    fv_enrollment_clear(&d);
}
/* Destruction: request persists DESTROY_PENDING, then burns the token and ends
 * DESTROYED. Already-destroyed is idempotent. */
static void destroy_lifecycle(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    /* Advance the journal to ACTIVE (provisioning leaves EMPTY). */
    fv_device_authority a=fv_enrollment_authority(&d);
    activate(&d);
    fv_device_state s;CHECK(a.load(a.context,&s)==0 && s.status==FV_ENROLLMENT_ACTIVE);
    CHECK(fv_enrollment_request_destruction(&d)==0);
    CHECK(fv_enrollment_open(&d)==0); /* terminal projection, OTP alone */
    CHECK(mask_state(&m,0)==FV_OTP_MASK_REVOKED);
    CHECK(fv_enrollment_token_blank(&d,0)==0); /* burned */
    CHECK(!load_state(&d,&s) && s.status==FV_ENROLLMENT_DESTROYED);
    /* Idempotent: destroying an already destroyed enrollment is a no-op success. */
    CHECK(fv_enrollment_request_destruction(&d)==0);
    CHECK(fv_enrollment_open(&d)==0);
    /* A new enrollment provisions the next blank token (slot 1). */
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(mask_state(&m,1)==FV_OTP_MASK_READY);
    CHECK(fv_enrollment_open(&d)==0);
    fv_device_state t;CHECK(!load_state(&d,&t));CHECK(t.token_slot==1);
    fv_enrollment_clear(&d);
}
/* Two READY tokens are ambiguous: a valid enrollment is provisioned and opens
 * first, then the conflicting READY slot is added and open/provision deny. */
static void ambiguous_ready_denied(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    /* Establish a valid enrollment that opens successfully. */
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(fv_enrollment_open(&d)==0);
    /* Add a second READY token at slot 1: two READY is ambiguous. */
    occupy_token(&m,1);plant_mask(&m,1,FV_OTP_MASK_READY);
    CHECK(fv_enrollment_open(&d)==-1);
    fv_enrollment_inventory inv=fv_enrollment_inspect(&d);
    CHECK(inv.mask_blank==0 && inv.tokens_blank==0);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==-1);
    fv_enrollment_clear(&d);
}
/* READY plus STARTED is ambiguous and must never select a journal: a valid
 * enrollment is provisioned and opens first, then the conflicting STARTED slot
 * is added and open/provision deny. */
static void ready_plus_started_denied(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(fv_enrollment_open(&d)==0);
    occupy_token(&m,5);plant_mask(&m,5,FV_OTP_MASK_STARTED);
    CHECK(fv_enrollment_open(&d)==-1);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==-1);
    fv_enrollment_clear(&d);
}
/* Legacy occupied token rows are excluded from allocation, not rewritten.
 * Slot 0 is the first allocation candidate: occupy it and snapshot its raw
 * contents, then prove the next blank slot is allocated and the legacy rows
 * are byte-for-byte unchanged. */
static void legacy_rows_excluded(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    occupy_token(&m,0);
    uint32_t snapshot[16];
    unsigned row=token_page(0)*64+token_row_offset(0);
    for(unsigned i=0;i<16;i++)snapshot[i]=m.otp[row+i];
    CHECK(fv_enrollment_token_blank(&d,0)==0); /* legacy occupied */
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    CHECK(mask_state(&m,1)==FV_OTP_MASK_READY); /* next blank slot, not legacy slot 0 */
    CHECK(mask_state(&m,0)==FV_OTP_MASK_UNUSED); /* legacy mask untouched */
    for(unsigned i=0;i<16;i++)CHECK(m.otp[row+i]==snapshot[i]); /* raw contents unchanged */
    CHECK(fv_enrollment_token_blank(&d,0)==0);
    fv_enrollment_clear(&d);
}
/* Binding is available for pre-provisioned (EMPTY) and ACTIVE enrollment. */
static void binding_works(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_provision(&d,random_bytes,&m)==0);
    uint8_t out[32];fv_device_authority a=fv_enrollment_authority(&d);
    /* EMPTY: binding key is the token, no volume check. */
    CHECK(!a.binding(a.context,id,0,out));
    /* Advance to ACTIVE and bind with the journal's volume id. */
    activate(&d);
    fv_device_state s;CHECK(!load_state(&d,&s));
    uint8_t vol[16];memcpy(vol,s.volume_id,16);
    uint8_t out2[32];CHECK(!a.binding(a.context,vol,0,out2));
    /* Wrong volume id is rejected for ACTIVE. */
    uint8_t bad[16];memset(bad,9,16);CHECK(a.binding(a.context,bad,0,out2)!=0);
    fv_enrollment_clear(&d);
}
/* Flash preparation refuses any occupied OTP row. */
static void prepare_flash_refuses_occupied(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    CHECK(fv_enrollment_prepare_flash(&d)==0);
    occupy_token(&m,9);
    CHECK(fv_enrollment_prepare_flash(&d)!=0);
    fv_enrollment_clear(&d);
}
/* Flash preparation refuses an unreadable OTP row even when flash holds data
 * and programming is otherwise permitted: assert refusal and no erasure. */
static void prepare_flash_refuses_unreadable_otp(void) {
    media m;init_media(&m);fv_enrollment_device d=open_device(&m);
    uint8_t page[256];memset(page,0xaa,sizeof(page));
    program(&m,0,page); /* flash now contains data */
    m.fail_read=1; /* OTP unreadable, programming otherwise permitted */
    CHECK(fv_enrollment_prepare_flash(&d)!=0);
    CHECK(m.erases==0); /* no erasure on read error */
    fv_enrollment_clear(&d);
}
int main(void) {
    virgin();
    provision_creates_ready();
    reprovision_is_noop();
    abandoned_started_recovered();
    destroy_lifecycle();
    ambiguous_ready_denied();
    ready_plus_started_denied();
    legacy_rows_excluded();
    binding_works();
    prepare_flash_refuses_occupied();
    prepare_flash_refuses_unreadable_otp();
    puts("Enrollment: provision, destruction, revocation, ambiguity and legacy-row coverage passed");
    return 0;
}
