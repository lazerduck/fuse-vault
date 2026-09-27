#include "fuse_vault/enrollment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint8_t flash[8192];uint32_t otp[4096];
    unsigned programs,erases,otp_writes,fail_program,fail_erase,fail_otp,partial,random;
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
    media *m=ctx;CHECK(row/64>=16 && row/64<=24);CHECK(row%64<=18);
    CHECK(!(m->otp[row]&~value));m->otp_writes++;
    if(m->otp_writes==m->fail_otp){m->otp[row]|=value&0xff;return -1;}
    m->otp[row]|=value;return 0;
}
static int read_secret(void *ctx,unsigned page,uint8_t out[32]) {
    media *m=ctx;CHECK(page>=16 && page<=24);if(m->denied)return -1;
    for(unsigned i=0;i<16;i++){uint32_t row=m->otp[page*64+i];out[2*i]=(uint8_t)row;out[2*i+1]=(uint8_t)(row>>8);}
    return 0;
}
static int write_secret(void *ctx,unsigned page,const uint8_t in[32]) {
    for(unsigned i=0;i<16;i++)if(raw_write(ctx,page*64+i,0x10000u|in[2*i]|(uint32_t)in[2*i+1]<<8))return -1;
    return 0;
}
static int can_program(void *ctx,unsigned page){CHECK(page>=16 && page<=24);return ((media*)ctx)->denied?-1:0;}
static int random_bytes(void *ctx,uint8_t *out,size_t n){media *m=ctx;for(size_t i=0;i<n;i++)out[i]=(uint8_t)++m->random;return 0;}
static const uint8_t id[16]={1,2,3},key[32]={9,8,7};
static void init_media(media *m){memset(m,0,sizeof(*m));memset(m->flash,255,8192);}
static fv_journal_io flash_io(media *m){return (fv_journal_io){m,read_flash,program,erase};}
static void init_device(fv_enrollment_device *d,media *m) {
    fv_enrollment_init(d,(fv_otp_io){m,raw_read,raw_write,read_secret,write_secret,can_program},flash_io(m),id);
}
static fv_device_state fresh(uint64_t seq) {
    fv_device_state s={.status=FV_ENROLLMENT_EMPTY,.sequence=seq,.policy={10,FV_LIMIT_DESTROY}};
    memcpy(s.device_id,id,16);return s;
}
static void journals(void) {
    media m;init_media(&m);fv_journal j;fv_journal_init(&j,flash_io(&m),key,id);fv_device_state out;
    CHECK(fv_journal_load(&j,&out)==FV_JOURNAL_EMPTY);
    for(unsigned i=1;i<=100;i++) {
        fv_device_state s=fresh(i);CHECK(!fv_journal_commit(&j,i-1,&s));CHECK(!fv_journal_load(&j,&out));CHECK(out.sequence==i);
    }
    CHECK(m.erases==12);fv_device_state next=fresh(101);unsigned calls=m.programs;
    CHECK(fv_journal_commit(&j,99,&next));CHECK(m.programs==calls);
    /* Wrong root/device, committed body tampering, unknown policy all deny. */
    j.key[0]^=1;CHECK(fv_journal_load(&j,&out)==FV_JOURNAL_CORRUPT);j.key[0]^=1;
    m.flash[(99%16)*512+112]^=1;CHECK(fv_journal_load(&j,&out)==FV_JOURNAL_CORRUPT);
    /* Model all cut points in each program page. Never report old state after
     * the new commit fully persisted, even if the I/O returned an error. */
    for(unsigned which=1;which<=2;which++)for(unsigned cut=0;cut<=256;cut++) {
        init_media(&m);fv_journal_init(&j,flash_io(&m),key,id);
        fv_device_state s=fresh(1);CHECK(!fv_journal_commit(&j,0,&s));s=fresh(2);
        m.fail_program=m.programs+which;m.partial=cut;
        CHECK(fv_journal_commit(&j,1,&s)!=0);int r=fv_journal_load(&j,&out);
        if(which==1 || cut==0){CHECK(r==0 && out.sequence==1);}
        else if(cut==256){CHECK(r==0 && out.sequence==2);}
        else CHECK(r==FV_JOURNAL_CORRUPT);
    }
    /* Torn erase only targets the older bank. Denial is acceptable when an old
     * commit is damaged; silently lowering the active sequence is not. */
    for(unsigned cut=0;cut<=4096;cut+=127) {
        init_media(&m);fv_journal_init(&j,flash_io(&m),key,id);
        for(unsigned i=1;i<=16;i++){fv_device_state s=fresh(i);CHECK(!fv_journal_commit(&j,i-1,&s));}
        m.fail_erase=m.erases+1;m.partial=cut;fv_device_state s=fresh(17);
        CHECK(fv_journal_commit(&j,16,&s));int r=fv_journal_load(&j,&out);
        CHECK(r==FV_JOURNAL_CORRUPT || (r==0 && out.sequence==16));
    }
}
static void preparation(void) {
    media m;fv_enrollment_device d;
    init_media(&m);init_device(&d,&m);
    CHECK(!fv_enrollment_prepare_flash(&d));CHECK(!m.erases);
    memset(m.flash,0,8192);
    fv_enrollment_inventory v=fv_enrollment_inspect(&d);
    CHECK(v.root_blank==1 && v.tokens_blank==1 && v.flash_blank==0);
    CHECK(fv_enrollment_open(&d)<0);
    CHECK(!fv_enrollment_prepare_flash(&d));CHECK(m.erases==2 && !m.otp_writes);
    CHECK(fv_enrollment_open(&d)==1);
    /* A single occupied row anywhere in root/token allocation forbids erasure. */
    for(unsigned row=16*64;row<25*64;row++) {
        init_media(&m);init_device(&d,&m);memset(m.flash,0,8192);m.otp[row]=1;
        CHECK(fv_enrollment_prepare_flash(&d));CHECK(!m.erases && !m.otp_writes);
    }
    init_media(&m);init_device(&d,&m);memset(m.flash,0,8192);m.denied=true;
    CHECK(fv_enrollment_prepare_flash(&d));CHECK(!m.erases);
    /* A torn initial erase can be retried only while all enrollment OTP is blank. */
    for(unsigned bank=1;bank<=2;bank++) {
        init_media(&m);init_device(&d,&m);memset(m.flash,0,8192);
        m.fail_erase=bank;m.partial=123;
        CHECK(fv_enrollment_prepare_flash(&d));CHECK(!m.otp_writes);
        m.fail_erase=0;CHECK(!fv_enrollment_prepare_flash(&d));CHECK(fv_enrollment_open(&d)==1);
        CHECK(!fv_enrollment_provision(&d,random_bytes,&m));unsigned erases=m.erases;
        CHECK(fv_enrollment_prepare_flash(&d));CHECK(m.erases==erases);
    }
}
static void enrollments(void) {
    media m;fv_enrollment_device d;init_media(&m);init_device(&d,&m);
    CHECK(fv_enrollment_open(&d)==1);CHECK(!m.otp_writes && !m.programs);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));CHECK(!fv_enrollment_open(&d));
    fv_device_authority a=fv_enrollment_authority(&d);fv_device_state s;
    CHECK(!a.load(a.context,&s));CHECK(s.status==FV_ENROLLMENT_EMPTY && s.sequence==1 && !s.token_slot);
    uint8_t b1[32],b2[32],volume[16]={99};CHECK(!a.binding(a.context,volume,0,b1));
    unsigned writes=m.otp_writes;CHECK(!fv_enrollment_provision(&d,random_bytes,&m));CHECK(m.otp_writes==writes);
    /* Establish an active header anchor, then restart all RAM adapter state. */
    s.status=FV_ENROLLMENT_ACTIVE;s.credential_generation=1;s.volume_id[0]=99;s.header_hash[0]=55;s.sequence++;
    CHECK(!a.commit(a.context,1,&s));fv_enrollment_clear(&d);init_device(&d,&m);CHECK(!fv_enrollment_open(&d));a=fv_enrollment_authority(&d);
    CHECK(!a.load(a.context,&s));CHECK(s.status==FV_ENROLLMENT_ACTIVE && s.sequence==2);
    CHECK(!a.binding(a.context,volume,0,b2));CHECK(!memcmp(b1,b2,32));CHECK(fv_enrollment_provision(&d,random_bytes,&m));
    /* Once active, missing journal NEVER reinitializes even with intact root. */
    uint8_t backup[8192];memcpy(backup,m.flash,8192);memset(m.flash,255,8192);
    CHECK(fv_enrollment_open(&d)<0);CHECK(fv_enrollment_provision(&d,random_bytes,&m));memcpy(m.flash,backup,8192);CHECK(!fv_enrollment_open(&d));
    /* Interrupted burn: revoked first, journal intent persists, restart resumes. */
    m.fail_otp=m.otp_writes+4;CHECK(fv_enrollment_request_destruction(&d));CHECK(m.otp[17*64+17]!=0);
    CHECK(a.binding(a.context,volume,0,b2));m.fail_otp=0;
    fv_enrollment_clear(&d);init_device(&d,&m);CHECK(!fv_enrollment_open(&d));a=fv_enrollment_authority(&d);
    fv_vault_platform p={.authority=a,.kdf_limits={60000,60000}};
    CHECK(fv_vault_recover(&p)==FV_VAULT_DENIED);CHECK(!a.load(a.context,&s));CHECK(s.status==FV_ENROLLMENT_DESTROYED);
    for(unsigned i=0;i<16;i++)CHECK(m.otp[17*64+i]==0xffffff);
    uint32_t oldroot[16];memcpy(oldroot,m.otp+16*64,sizeof(oldroot));
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));CHECK(!a.load(a.context,&s));CHECK(s.token_slot==1 && s.status==FV_ENROLLMENT_EMPTY);
    CHECK(!memcmp(oldroot,m.otp+16*64,sizeof(oldroot)));CHECK(a.binding(a.context,volume,0,b2));
    CHECK(!a.binding(a.context,volume,1,b2));CHECK(memcmp(b1,b2,32));
    /* Failure after initial journal commit but before activation is resumable. */
    init_media(&m);init_device(&d,&m);m.fail_otp=35;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));m.fail_otp=0;
    init_device(&d,&m);CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    /* Incomplete token skipped; incomplete ROOT must not be silently replaced. */
    init_media(&m);init_device(&d,&m);m.fail_otp=20;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));m.fail_otp=0;
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));a=fv_enrollment_authority(&d);
    CHECK(!a.load(a.context,&s));CHECK(s.token_slot==1);
    init_media(&m);init_device(&d,&m);m.fail_otp=3;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));m.fail_otp=0;writes=m.otp_writes;
    CHECK(fv_enrollment_provision(&d,random_bytes,&m));CHECK(m.otp_writes==writes);
    init_media(&m);init_device(&d,&m);m.denied=true;CHECK(fv_enrollment_provision(&d,random_bytes,&m));CHECK(!m.otp_writes);
}
static uint8_t card[2200*512];
static fv_block_result_t sd_read(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *out) {
    (void)d;if(lba>=2200 || n>2200-lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(out,card+lba*512,(size_t)n*512);return FV_BLOCK_OK;
}
static fv_block_result_t sd_write(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *in) {
    (void)d;if(lba>=2200 || n>2200-lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(card+lba*512,in,(size_t)n*512);return FV_BLOCK_OK;
}
static fv_block_result_t sd_sync(fv_block_device_t *d){(void)d;return FV_BLOCK_OK;}
static uint64_t sd_count(const fv_block_device_t *d){(void)d;return 2200;}
static bool sd_present(const fv_block_device_t *d){(void)d;return true;}
static void full_pipeline(void) {
    media m;init_media(&m);fv_enrollment_device d;init_device(&d,&m);
    CHECK(!fv_enrollment_provision(&d,random_bytes,&m));
    const fv_block_device_ops_t ops={sd_read,sd_write,sd_sync,sd_count,sd_present};
    fv_block_device_t disk={&ops,NULL};
    fv_vault_platform p={.sd=&disk,.authority=fv_enrollment_authority(&d),
        .random=random_bytes,.random_context=&m,.kdf_limits={1,100}};
    const uint16_t algorithms[4]={1,2,1,2};const uint8_t secret[]="original",next[]="replacement";
    fv_vault v={0};alignas(4) uint8_t data[1024],cipher[1024],old_headers[8192];
    CHECK(!fv_vault_create(&p,64,algorithms,4,1,3,fv_auth_policy_default(),secret,8));
    CHECK(!fv_vault_unlock(&v,&p,secret,8));memset(data,37,sizeof(data));
    CHECK(fv_vault_write(&v,5,2,data)==FV_BLOCK_OK);memcpy(cipher,card+(2069+5)*512,1024);
    memcpy(old_headers,card,8192);fv_vault_lock(&v);fv_enrollment_clear(&d);
    init_device(&d,&m);CHECK(!fv_enrollment_open(&d));CHECK(!fv_vault_recover(&p));
    CHECK(!fv_vault_unlock(&v,&p,secret,8));CHECK(fv_vault_read(&v,5,2,data)==FV_BLOCK_OK);
    for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==37);
    CHECK(!fv_vault_change_credential(&v,&p,secret,8,next,11,1,3,fv_auth_policy_default(),false));
    CHECK(!memcmp(cipher,card+(2069+5)*512,1024));
    fv_enrollment_clear(&d);init_device(&d,&m);CHECK(!fv_enrollment_open(&d));
    CHECK(fv_vault_unlock(&v,&p,secret,8)==FV_VAULT_AUTH);
    fv_device_state state;CHECK(!p.authority.load(p.authority.context,&state));CHECK(state.attempts==1);
    fv_enrollment_clear(&d);init_device(&d,&m);CHECK(!fv_enrollment_open(&d));
    CHECK(!p.authority.load(p.authority.context,&state));CHECK(state.attempts==1);
    CHECK(!fv_vault_unlock(&v,&p,next,11));CHECK(fv_vault_read(&v,5,2,data)==FV_BLOCK_OK);
    for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==37);
    fv_vault_lock(&v);
    /* Header rollback cannot reset flash attempts or restore the old credential. */
    uint8_t current_headers[8192];memcpy(current_headers,card,8192);memcpy(card,old_headers,8192);
    CHECK(fv_vault_unlock(&v,&p,secret,8)==FV_VAULT_IO);memcpy(card,current_headers,8192);
    /* Durable final pending attempt resumes without any SD and burns the token. */
    CHECK(!p.authority.load(p.authority.context,&state));uint64_t previous=state.sequence;
    state.sequence++;state.attempts=10;state.attempt_pending=true;
    CHECK(!p.authority.commit(p.authority.context,previous,&state));
    fv_enrollment_clear(&d);init_device(&d,&m);CHECK(!fv_enrollment_open(&d));p.sd=NULL;
    CHECK(fv_vault_recover(&p)==FV_VAULT_DENIED);
    CHECK(!p.authority.load(p.authority.context,&state));CHECK(state.status==FV_ENROLLMENT_DESTROYED);
    p.sd=&disk;memcpy(card,old_headers,8192);CHECK(fv_vault_unlock(&v,&p,secret,8)==FV_VAULT_DENIED);
}
int main(void){preparation();journals();enrollments();full_pipeline();puts("Journal/OTP: rollover, 514 page-program cuts, torn erases, provisioning, restart, revocation and slot rotation passed");return 0;}
