#include "fuse_vault/vault.h"
#include <stdio.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define CAPACITY 2200u
static const uint8_t password[]="test password",replacement[]="different password";
static const uint16_t algorithms[4]={1,2,1,2};
static bool zero(const void *p,size_t n){const uint8_t *b=p;while(n--)if(*b++)return false;return true;}
typedef struct {
    FILE *sd,*state;
    fv_block_device_t device;
    fv_vault_platform platform;
    fv_vault session;
    uint8_t root[32],token[32];
    unsigned random_byte,commits,fail_commit,bindings,destroys,header_writes,fail_header_write;
    unsigned syncs,fail_sync,corrupt_readback;
    bool uncertain_commit,present,destroyed,fail_destroy;
} fixture;
static fv_block_result_t read_blocks(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *out) {
    fixture *f=d->context;
    if(!f->present || lba>=CAPACITY || n>CAPACITY-lba)return FV_BLOCK_ERROR_IO;
    if(fseek(f->sd,(long)(512*lba),SEEK_SET) || fread(out,512,n,f->sd)!=n)return FV_BLOCK_ERROR_IO;
    if(f->corrupt_readback && f->header_writes==f->corrupt_readback && (lba==0 || lba==8))out[300]^=1;
    return FV_BLOCK_OK;
}
static fv_block_result_t write_blocks(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *in) {
    fixture *f=d->context;
    if(!f->present || lba>=CAPACITY || n>CAPACITY-lba)return FV_BLOCK_ERROR_IO;
    if(lba==0 || lba==8)if(++f->header_writes==f->fail_header_write)return FV_BLOCK_ERROR_IO;
    if(fseek(f->sd,(long)(512*lba),SEEK_SET) || fwrite(in,512,n,f->sd)!=n)return FV_BLOCK_ERROR_IO;
    return FV_BLOCK_OK;
}
static fv_block_result_t sync_device(fv_block_device_t *d) {
    fixture *f=d->context;
    if(++f->syncs==f->fail_sync || fflush(f->sd))return FV_BLOCK_ERROR_IO;
    return FV_BLOCK_OK;
}
static uint64_t capacity(const fv_block_device_t *d){(void)d;return CAPACITY;}
static bool present(const fv_block_device_t *d){return ((const fixture*)d->context)->present;}
static const fv_block_device_ops_t device_ops={read_blocks,write_blocks,sync_device,capacity,present};
static int load(void *context,fv_device_state *s) {
    fixture *f=context;rewind(f->state);return fread(s,sizeof(*s),1,f->state)!=1;
}
static void persist(fixture *f,const fv_device_state *s) {
    rewind(f->state);CHECK(fwrite(s,sizeof(*s),1,f->state)==1);CHECK(!fflush(f->state));
}
static int commit(void *context,uint64_t previous,const fv_device_state *s) {
    fixture *f=context;fv_device_state old;CHECK(!load(f,&old));CHECK(old.sequence==previous);CHECK(s->sequence==previous+1);
    bool fail=++f->commits==f->fail_commit;
    if(!fail || f->uncertain_commit)persist(f,s);
    return fail?-1:0;
}
static int binding(void *context,const uint8_t id[16],uint32_t slot,uint8_t out[32]) {
    fixture *f=context;f->bindings++;
    if(f->destroyed || slot!=3)return -1;
    return fv_vault_binding(f->root,f->token,slot,id,out);
}
static int destroy(void *context,uint32_t slot) {
    fixture *f=context;CHECK(slot==3);f->destroys++;
    if(f->fail_destroy)return -1;
    f->destroyed=true;memset(f->token,0,32);return 0;
}
static int random_bytes(void *context,uint8_t *out,size_t n) {
    fixture *f=context;for(size_t i=0;i<n;i++)out[i]=(uint8_t)(++f->random_byte);return 0;
}
static void init(fixture *f) {
    memset(f,0,sizeof(*f));f->sd=tmpfile();f->state=tmpfile();CHECK(f->sd && f->state);f->present=true;
    uint8_t block[512]={0};for(unsigned i=0;i<CAPACITY;i++)CHECK(fwrite(block,512,1,f->sd)==1);CHECK(!fflush(f->sd));
    for(unsigned i=0;i<32;i++){f->root[i]=(uint8_t)i;f->token[i]=(uint8_t)(32+i);}
    f->device=(fv_block_device_t){&device_ops,f};
    f->platform=(fv_vault_platform){.sd=&f->device,.authority={f,load,commit,binding,destroy},
        .random=random_bytes,.random_context=f,.kdf_limits={1,100}};
    fv_device_state s={.status=FV_ENROLLMENT_EMPTY,.device_id={1},.token_slot=3};persist(f,&s);
}
static void finish(fixture *f){fv_vault_lock(&f->session);CHECK(zero(&f->session,sizeof(f->session)));fclose(f->sd);fclose(f->state);}
static void create(fixture *f,fv_auth_policy policy) {
    CHECK(fv_vault_create(&f->platform,64,algorithms,4,1,3,policy,password,sizeof(password)-1)==FV_VAULT_OK);
    CHECK(!f->session.unlocked);
    CHECK(fv_vault_create(&f->platform,64,algorithms,4,1,3,policy,password,sizeof(password)-1)==FV_VAULT_DENIED);
}
static fv_vault_result unlock(fixture *f,const uint8_t *secret,size_t n) {
    return fv_vault_unlock(&f->session,&f->platform,secret,n);
}
static fv_vault_result change(fixture *f) {
    return fv_vault_change_credential(&f->session,&f->platform,password,sizeof(password)-1,
        replacement,sizeof(replacement)-1,1,4,fv_auth_policy_default(),false);
}
static void check_plaintext(fixture *f) {
    alignas(4) uint8_t data[1024];
    CHECK(fv_vault_read(&f->session,7,2,data)==FV_BLOCK_OK);
    for(unsigned i=0;i<1024;i++)CHECK(data[i]==(uint8_t)(i*7));
}
static void lifecycle(void) {
    fixture f;init(&f);create(&f,fv_auth_policy_default());
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    alignas(4) uint8_t data[1024],old_header[512],new_header[512],ciphertext[1024];
    CHECK(fv_vault_read(&f.session,7,2,data)==FV_BLOCK_OK);CHECK(zero(data,sizeof(data)));
    for(unsigned i=0;i<1024;i++)data[i]=(uint8_t)(i*7);
    CHECK(fv_vault_write(&f.session,7,2,data)==FV_BLOCK_OK);CHECK(zero(data,sizeof(data)));check_plaintext(&f);
    CHECK(read_blocks(&f.device,0,1,old_header)==FV_BLOCK_OK);
    CHECK(read_blocks(&f.device,2069+7,2,ciphertext)==FV_BLOCK_OK);
    uint8_t original_vmk[32];memcpy(original_vmk,f.session.vmk,32);
    fv_vault_lock(&f.session);CHECK(zero(&f.session,sizeof(f.session)));
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);check_plaintext(&f);
    CHECK(change(&f)==FV_VAULT_OK);CHECK(!f.session.unlocked);
    CHECK(read_blocks(&f.device,0,1,new_header)==FV_BLOCK_OK);CHECK(memcmp(new_header,old_header,512));CHECK(!memcmp(new_header,old_header,128));
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_AUTH);
    CHECK(unlock(&f,replacement,sizeof(replacement)-1)==FV_VAULT_OK);check_plaintext(&f);CHECK(!memcmp(original_vmk,f.session.vmk,32));
    CHECK(read_blocks(&f.device,2069+7,2,data)==FV_BLOCK_OK);CHECK(!memcmp(data,ciphertext,1024));
    /* Replay both old headers cannot restore an old credential, and isn't a guess. */
    fv_device_state before,after;CHECK(!load(&f,&before));
    CHECK(write_blocks(&f.device,0,1,old_header)==FV_BLOCK_OK);CHECK(write_blocks(&f.device,8,1,old_header)==FV_BLOCK_OK);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_IO);CHECK(!load(&f,&after));CHECK(before.sequence==after.sequence);
    CHECK(write_blocks(&f.device,8,1,new_header)==FV_BLOCK_OK);
    CHECK(unlock(&f,replacement,sizeof(replacement)-1)==FV_VAULT_OK);check_plaintext(&f);
    CHECK(!load(&f,&before));CHECK(fv_vault_repair_headers(&f.session)==FV_VAULT_OK);
    CHECK(!load(&f,&after));CHECK(before.sequence==after.sequence);
    CHECK(read_blocks(&f.device,0,1,old_header)==FV_BLOCK_OK);CHECK(!memcmp(old_header,new_header,512));
    /* Corrupt the SECOND sector: no authenticated first-sector plaintext escapes. */
    ciphertext[700]^=1;CHECK(write_blocks(&f.device,2069+7,2,ciphertext)==FV_BLOCK_OK);
    CHECK(fv_vault_read(&f.session,7,2,data)==FV_BLOCK_ERROR_INTEGRITY);CHECK(zero(data,1024));CHECK(f.session.unlocked);
    ciphertext[700]^=1;CHECK(write_blocks(&f.device,2069+7,2,ciphertext)==FV_BLOCK_OK);check_plaintext(&f);
    f.present=false;CHECK(fv_vault_read(&f.session,7,2,data)==FV_BLOCK_ERROR_NOT_READY);CHECK(zero(data,1024));CHECK(zero(&f.session,sizeof(f.session)));
    finish(&f);
}
static void attempts(void) {
    fixture f;init(&f);create(&f,fv_auth_policy_default());
    for(unsigned i=1;i<10;i++) {
        CHECK(unlock(&f,(const uint8_t*)"wrong",5)==FV_VAULT_AUTH);
        fv_device_state s;CHECK(!load(&f,&s));CHECK(s.attempts==i && !s.attempt_pending);
    }
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    fv_device_state s;CHECK(!load(&f,&s));CHECK(!s.attempts);fv_vault_lock(&f.session);
    for(unsigned i=1;i<10;i++)CHECK(unlock(&f,(const uint8_t*)"wrong",5)==FV_VAULT_AUTH);
    CHECK(unlock(&f,(const uint8_t*)"wrong",5)==FV_VAULT_DENIED);CHECK(f.destroyed && f.destroys==1);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_DENIED);
    CHECK(!load(&f,&s));CHECK(s.status==FV_ENROLLMENT_DESTROYED);finish(&f);
    /* Interrupted final attempt acts on boot even with NO SD; failed destruction retries. */
    init(&f);create(&f,fv_auth_policy_default());CHECK(!load(&f,&s));s.attempts=10;s.attempt_pending=true;persist(&f,&s);
    f.present=false;f.fail_destroy=true;CHECK(fv_vault_recover(&f.platform)==FV_VAULT_STATE);
    CHECK(!load(&f,&s));CHECK(s.status==FV_ENROLLMENT_DESTROY_PENDING);
    f.fail_destroy=false;CHECK(fv_vault_recover(&f.platform)==FV_VAULT_DENIED);CHECK(f.destroyed);finish(&f);
    init(&f);create(&f,(fv_auth_policy){2,FV_LIMIT_LOCKOUT});
    CHECK(unlock(&f,(const uint8_t*)"wrong",5)==FV_VAULT_AUTH);
    CHECK(unlock(&f,(const uint8_t*)"wrong",5)==FV_VAULT_DENIED);CHECK(!f.destroyed);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_DENIED);finish(&f);
}
static void failures(void) {
    /* Before charge: no credential evaluation. Uncertain commit retains charge. */
    for(unsigned uncertain=0;uncertain<2;uncertain++) {
        fixture f;init(&f);create(&f,fv_auth_policy_default());f.commits=0;f.fail_commit=1;f.uncertain_commit=uncertain;
        unsigned calls=f.bindings;
        CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_STATE);CHECK(f.bindings==calls);CHECK(!f.session.unlocked);
        f.fail_commit=0;CHECK(fv_vault_recover(&f.platform)==FV_VAULT_OK);
        fv_device_state s;CHECK(!load(&f,&s));CHECK(s.attempts==uncertain && !s.attempt_pending);
        CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);finish(&f);
    }
    /* Failed successful-attempt reset cannot publish a session; final charge survives. */
    fixture f;init(&f);create(&f,fv_auth_policy_default());fv_device_state s;CHECK(!load(&f,&s));s.attempts=9;persist(&f,&s);
    f.commits=0;f.fail_commit=2;CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_STATE);CHECK(!f.session.unlocked);
    f.fail_commit=0;CHECK(fv_vault_recover(&f.platform)==FV_VAULT_DENIED);CHECK(f.destroyed);finish(&f);
    /* Failed header write before anchor keeps old credential; mirror failure keeps new. */
    for(unsigned fail=1;fail<=2;fail++) {
        init(&f);create(&f,fv_auth_policy_default());f.header_writes=0;f.fail_header_write=fail;
        CHECK(change(&f)==(fail==1?FV_VAULT_IO:FV_VAULT_CHANGED_NEEDS_MIRROR));CHECK(!f.session.unlocked);
        f.fail_header_write=0;
        CHECK(unlock(&f,fail==1?password:replacement,fail==1?sizeof(password)-1:sizeof(replacement)-1)==FV_VAULT_OK);
        finish(&f);
    }
    /* A reported sync failure or mismatched readback cannot advance the anchor. */
    for(unsigned mode=0;mode<2;mode++) {
        init(&f);create(&f,fv_auth_policy_default());f.header_writes=0;f.syncs=0;
        if(mode)f.corrupt_readback=1;else f.fail_sync=1;
        CHECK(change(&f)==FV_VAULT_IO);CHECK(!f.session.unlocked);
        f.corrupt_readback=0;f.fail_sync=0;
        CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);finish(&f);
    }
    /* Failure at anchor, including commit-durable-but-return-error. */
    for(unsigned uncertain=0;uncertain<2;uncertain++) {
        init(&f);create(&f,fv_auth_policy_default());f.commits=0;f.fail_commit=3;f.uncertain_commit=uncertain;
        CHECK(change(&f)==FV_VAULT_STATE);CHECK(!f.session.unlocked);f.fail_commit=0;
        CHECK(unlock(&f,uncertain?replacement:password,uncertain?sizeof(replacement)-1:sizeof(password)-1)==FV_VAULT_OK);
        finish(&f);
    }
    init(&f);create(&f,fv_auth_policy_default());CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    alignas(4) uint8_t data[512];memset(data,42,512);f.syncs=0;f.fail_sync=1;
    CHECK(fv_vault_write(&f.session,0,1,data)==FV_BLOCK_ERROR_IO);CHECK(zero(data,512));CHECK(zero(&f.session,sizeof(f.session)));finish(&f);
}
static void batches_and_policy(void) {
    fixture f;init(&f);create(&f,fv_auth_policy_default());
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    alignas(4) uint8_t data[64*512];
    for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*19+7);
    CHECK(fv_vault_write(&f.session,0,64,data)==FV_BLOCK_OK);CHECK(zero(data,sizeof(data)));
    fv_vault_lock(&f.session);CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    CHECK(fv_vault_read(&f.session,0,64,data)==FV_BLOCK_OK);
    for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==(uint8_t)(i*19+7));
    CHECK(fv_vault_read(&f.session,63,2,data)==FV_BLOCK_ERROR_OUT_OF_RANGE);CHECK(zero(data,1024));CHECK(f.session.unlocked);
    CHECK(fv_vault_change_credential(&f.session,&f.platform,password,sizeof(password)-1,replacement,sizeof(replacement)-1,
        1,3,(fv_auth_policy){20,FV_LIMIT_LOCKOUT},false)==FV_VAULT_INVALID);CHECK(!f.session.unlocked);
    CHECK(fv_vault_change_credential(&f.session,&f.platform,password,sizeof(password)-1,replacement,sizeof(replacement)-1,
        1,3,(fv_auth_policy){20,FV_LIMIT_LOCKOUT},true)==FV_VAULT_OK);
    fv_device_state s;CHECK(!load(&f,&s));CHECK(s.policy.max_attempts==20 && s.policy.limit_action==FV_LIMIT_LOCKOUT);
    CHECK(unlock(&f,replacement,sizeof(replacement)-1)==FV_VAULT_OK);
    finish(&f);
}
/* Build an original-layout envelope independently of the new creation path. */
static void legacy_volume(void) {
    fixture f;init(&f);fv_device_state state;CHECK(!load(&f,&state));
    fv_envelope_config c={.volume={.layout_version=1,.volume_id={7},.logical_blocks=64,.layer_count=4},
        .credential_generation=1,.credential_profile=1,.iterations=3,.token_slot=3,.policy={10,FV_LIMIT_DESTROY}};
    memcpy(c.device_id,state.device_id,16);memcpy(c.volume.cipher_ids,algorithms,sizeof(algorithms));
    uint8_t vmk[32]={8},bound[32];alignas(4) uint8_t header[512],data[512];
    CHECK(!binding(&f,c.volume.volume_id,3,bound));
    CHECK(!fv_envelope_seal(&c,CAPACITY,f.platform.kdf_limits,bound,password,sizeof(password)-1,
        vmk,random_bytes,&f,header));
    CHECK(write_blocks(&f.device,0,1,header)==FV_BLOCK_OK);
    CHECK(write_blocks(&f.device,8,1,header)==FV_BLOCK_OK);CHECK(sync_device(&f.device)==FV_BLOCK_OK);
    state.status=FV_ENROLLMENT_ACTIVE;state.credential_generation=1;state.policy=c.policy;
    memcpy(state.volume_id,c.volume.volume_id,16);CHECK(!mbedtls_sha256(header,512,state.header_hash,0));persist(&f,&state);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);CHECK(!f.session.store.bitmap_blocks);
    memset(data,0x49,512);CHECK(fv_vault_write(&f.session,0,1,data)==FV_BLOCK_OK);
    fv_vault_lock(&f.session);CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    CHECK(fv_vault_read(&f.session,0,1,data)==FV_BLOCK_OK);for(unsigned i=0;i<512;i++)CHECK(data[i]==0x49);
    CHECK(change(&f)==FV_VAULT_OK);CHECK(unlock(&f,replacement,sizeof(replacement)-1)==FV_VAULT_OK);
    CHECK(f.session.config.volume.layout_version==1 && !f.session.store.bitmap_blocks);
    CHECK(fv_vault_read(&f.session,0,1,data)==FV_BLOCK_OK);for(unsigned i=0;i<512;i++)CHECK(data[i]==0x49);
    finish(&f);
}
static void credential_methods(void){
    fixture f;init(&f);create(&f,fv_auth_policy_default());
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    alignas(4) uint8_t data[512];memset(data,0x73,512);CHECK(fv_vault_write(&f.session,0,1,data)==FV_BLOCK_OK);
    uint8_t vmk[32];memcpy(vmk,f.session.vmk,32);fv_vault_lock(&f.session);
    const uint8_t wheel[]={0,99,42,7},words[]={0,63,12,28},pattern[]={1,2,3,4,1,2,3,4};
    const uint8_t *inputs[]={password,wheel,words,pattern};size_t lengths[]={sizeof(password)-1,4,4,8};
    const uint16_t profiles[]={1,3,4,2};
    for(unsigned i=0;i<4;i++){
        unsigned commits=f.commits;uint16_t profile=0;
        CHECK(fv_vault_credential_profile(&f.platform,&profile)==FV_VAULT_OK && profile==profiles[i]);
        CHECK(f.commits==commits); /* Merely selecting the entry UI spends no guess. */
        if(i){
            CHECK(unlock(&f,inputs[i],lengths[i])==FV_VAULT_OK);
            CHECK(!memcmp(vmk,f.session.vmk,32));CHECK(fv_vault_read(&f.session,0,1,data)==FV_BLOCK_OK);
            for(unsigned j=0;j<512;j++)CHECK(data[j]==0x73);
            fv_vault_lock(&f.session);
        }
        if(i<3){
            CHECK(fv_vault_change_credential(&f.session,&f.platform,inputs[i],lengths[i],inputs[i+1],lengths[i+1],profiles[i+1],3,fv_auth_policy_default(),false)==FV_VAULT_OK);
            fv_device_state state;CHECK(!load(&f,&state));CHECK(state.token_slot==3 && !f.destroys && !state.attempts);
        }
    }
    unsigned commits=f.commits;uint16_t hinted=99;
    for(unsigned slot=0;slot<2;slot++){
        CHECK(read_blocks(&f.device,slot*8,1,data)==FV_BLOCK_OK);data[152]^=1;
        CHECK(write_blocks(&f.device,slot*8,1,data)==FV_BLOCK_OK);
    }
    CHECK(fv_vault_credential_profile(&f.platform,&hinted)==FV_VAULT_IO && !hinted);
    CHECK(f.commits==commits); /* Forged method hints do not consume attempts. */
    finish(&f);
}
int main(void) {
    lifecycle();attempts();failures();batches_and_policy();legacy_volume();credential_methods();
    puts("Vault: file-backed lifecycle, rewrap/replay, attempt/destruction and commit/SD failure injection passed");
    return 0;
}
