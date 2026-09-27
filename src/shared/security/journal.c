#include "fuse_vault/journal.h"
#include <mbedtls/platform_util.h>
#include <string.h>
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool filled(const uint8_t *p,size_t n,uint8_t v){while(n--)if(*p++!=v)return false;return true;}
int fv_journal_key(const uint8_t root[32],const uint8_t id[16],uint8_t out[32]) {
    uint8_t prk[32];static const uint8_t label[]="FV2/device-state-mac/v1";
    if(!root || !id || !out)return -1;
    int r=fv_hkdf_extract(id,16,root,32,prk);
    if(!r)r=fv_hkdf_expand(prk,label,sizeof(label),out,32);
    mbedtls_platform_zeroize(prk,32);return r;
}
void fv_journal_init(fv_journal *j,fv_journal_io io,const uint8_t key[32],const uint8_t id[16]) {
    memset(j,0,sizeof(*j));j->io=io;memcpy(j->key,key,32);memcpy(j->device_id,id,16);
}
void fv_journal_clear(fv_journal *j){mbedtls_platform_zeroize(j,sizeof(*j));}
static int tag(fv_journal *j,const char *label,size_t n,const uint8_t *data,size_t bytes,uint8_t out[32]) {
    fv_hmac h={0};int r=fv_hmac_init(&h,j->key,32);
    if(!r)r=fv_hmac_compute(&h,(const uint8_t*)label,n,data,bytes,out);
    fv_hmac_clear(&h);return r;
}
static int body(fv_journal *j,const fv_device_state *s,uint8_t out[256]) {
    memset(out,0,256);
    if(!s->sequence || memcmp(s->device_id,j->device_id,16) || s->status<FV_ENROLLMENT_EMPTY ||
       s->status>FV_ENROLLMENT_DESTROYED || s->token_slot>=8)return -1;
    if(s->status==FV_ENROLLMENT_EMPTY) {
        if(s->credential_generation || s->attempts || s->attempt_pending ||
           !filled(s->volume_id,16,0) || !filled(s->header_hash,32,0))return -1;
    } else if(!s->credential_generation || s->attempts>s->policy.max_attempts ||
              (s->attempt_pending && !s->attempts) || filled(s->volume_id,16,0) || filled(s->header_hash,32,0))return -1;
    if(!fv_auth_policy_encode(&s->policy,out+96))return -1;
    memcpy(out,"FV2STATE",8);put(out+8,1,2);put(out+10,256,2);put(out+12,s->status,4);put(out+16,s->sequence,8);
    memcpy(out+24,s->device_id,16);memcpy(out+40,s->volume_id,16);put(out+56,s->credential_generation,8);
    memcpy(out+64,s->header_hash,32);put(out+112,s->attempts,4);put(out+116,s->attempt_pending,4);put(out+120,s->token_slot,4);
    static const char domain[]="FV2/device-state/body/v1";
    return tag(j,domain,sizeof(domain),out,224,out+224);
}
static int marker(fv_journal *j,const uint8_t b[256],uint8_t out[256]) {
    memset(out,0,256);memcpy(out,"FV2COMIT",8);memcpy(out+8,b+16,8);
    if(mbedtls_sha256(b,256,out+16,0))return -1;
    static const char domain[]="FV2/device-state/commit/v1";
    return tag(j,domain,sizeof(domain),out,224,out+224);
}
static int decode(fv_journal *j,const uint8_t b[512],fv_device_state *s) {
    fv_device_state candidate={0};uint8_t canonical[512];
    candidate.status=(fv_enrollment)get(b+12,4);candidate.sequence=get(b+16,8);
    memcpy(candidate.device_id,b+24,16);memcpy(candidate.volume_id,b+40,16);candidate.credential_generation=get(b+56,8);
    memcpy(candidate.header_hash,b+64,32);candidate.attempts=(uint32_t)get(b+112,4);
    if(get(b+116,4)>1 || !fv_auth_policy_decode(b+96,16,&candidate.policy))return -1;
    candidate.attempt_pending=get(b+116,4)!=0;candidate.token_slot=(uint32_t)get(b+120,4);
    if(body(j,&candidate,canonical) || marker(j,canonical,canonical+256) || memcmp(b,canonical,512))return -1;
    *s=candidate;return 0;
}
typedef struct {fv_device_state latest;int latest_index,last_used[2],first_blank;bool found;} scan_result;
static int scan(fv_journal *j,scan_result *r) {
    memset(r,0,sizeof(*r));r->latest_index=r->last_used[0]=r->last_used[1]=r->first_blank=-1;
    uint8_t record[512],latest_record[512];
    for(unsigned i=0;i<16;i++) {
        if(j->io.read(j->io.context,i*512,record,512))return FV_JOURNAL_IO;
        if(filled(record,512,255)){if(r->first_blank<0)r->first_blank=(int)i;continue;}
        r->last_used[i/8]=(int)i;
        /* Only an entirely untouched commit page proves no commit was attempted.
         * A partially programmed/damaged commit is ambiguous: deny, never fall back. */
        if(filled(record+256,256,255))continue;
        fv_device_state s;
        if(decode(j,record,&s))return FV_JOURNAL_CORRUPT;
        if(r->found && s.sequence==r->latest.sequence && memcmp(record,latest_record,512))return FV_JOURNAL_CORRUPT;
        if(!r->found || s.sequence>r->latest.sequence) {
            r->found=true;r->latest=s;r->latest_index=(int)i;memcpy(latest_record,record,512);
        }
    }
    return r->found?FV_JOURNAL_OK:FV_JOURNAL_EMPTY;
}
int fv_journal_load(fv_journal *j,fv_device_state *s) {
    memset(s,0,sizeof(*s));scan_result r;int result=scan(j,&r);
    if(!result)*s=r.latest;
    return result;
}
int fv_journal_commit(fv_journal *j,uint64_t previous,const fv_device_state *s) {
    if(!j || !s || !j->io.read || !j->io.program || !j->io.erase || previous==UINT64_MAX || s->sequence!=previous+1)return FV_JOURNAL_CORRUPT;
    scan_result r;int status=scan(j,&r);
    if(status<0 || (status==FV_JOURNAL_EMPTY?previous!=0:r.latest.sequence!=previous))return FV_JOURNAL_CORRUPT;
    alignas(4) uint8_t record[512],verify[512];
    if(body(j,s,record) || marker(j,record,record+256))return FV_JOURNAL_CORRUPT;
    int index=r.first_blank;
    if(r.found) {
        unsigned bank=(unsigned)r.latest_index/8;
        index=r.last_used[bank]+1;
        if(index>=(int)((bank+1)*8)) {
            bank^=1;
            if(j->io.erase(j->io.context,bank))return FV_JOURNAL_IO;
            for(unsigned i=0;i<8;i++) {
                if(j->io.read(j->io.context,bank*4096+i*512,verify,512) || !filled(verify,512,255))return FV_JOURNAL_IO;
            }
            index=(int)bank*8;
        }
    }
    if(index<0)return FV_JOURNAL_CORRUPT;
    uint32_t offset=(uint32_t)index*512;
    if(j->io.read(j->io.context,offset,verify,512) || !filled(verify,512,255))return FV_JOURNAL_CORRUPT;
    if(j->io.program(j->io.context,offset,record) || j->io.read(j->io.context,offset,verify,256) || memcmp(record,verify,256))return FV_JOURNAL_IO;
    if(j->io.program(j->io.context,offset+256,record+256) || j->io.read(j->io.context,offset,verify,512) || memcmp(record,verify,512))return FV_JOURNAL_IO;
    return FV_JOURNAL_OK;
}
