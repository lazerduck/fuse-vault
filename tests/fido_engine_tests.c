#include "fuse_vault/fido_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/rand.h>
static uint8_t store[FV_FIDO_STORE_BYTES];
#ifdef FV_FIDO_ENCRYPTED_TEST
#include "fido_test_platform.h"
static fv_fido_fixture fixture;
static fv_fido_store encrypted;
#else
static uint8_t persisted[FV_FIDO_STORE_BYTES];
#endif
/* Simulation controls: this translation unit is never linked into firmware. */
static bool uv_allowed = true, rng_allowed = true, cancelled;
static uint32_t clock_ms = 1;
static bool verify_user(void *c, const uint8_t *rp) { (void)c; (void)rp; return uv_allowed; }
static uint8_t uv_retries(void *c) { (void)c; return 10; } 
static bool is_cancelled(void *c) { (void)c; return cancelled; }
static unsigned commits;
static bool commit_allowed = true;
static int presence_result;
static bool random_bytes(void *c,uint8_t *out,size_t n) { (void)c; return rng_allowed && RAND_bytes(out,(int)n)==1; }
static bool commit(void *c,const uint8_t *in,size_t n) {
    (void)c; assert(n==sizeof(store));
#ifdef FV_FIDO_ENCRYPTED_TEST
    /* Fail actual media I/O rather than bypassing the encrypted store callback. */
    if (!commit_allowed) fixture.fail_write=fixture.writes+1;
    if (fv_fido_store_commit(&encrypted,in)!=FV_FIDO_STORE_OK) return false;
#else
    if (!commit_allowed) return false;
    memcpy(persisted,in,n);
#endif
    ++commits;return true;
}
static void reload(void) {
#ifdef FV_FIDO_ENCRYPTED_TEST
    fv_fido_store_close(&encrypted);
    fv_vault_lock(&fixture.vault);
    fv_fido_fixture_io_reset(&fixture);
    fv_fido_fixture_unlock(&fixture);
    assert(fv_fido_store_open(&encrypted,&fixture.vault,store)==FV_FIDO_STORE_OK);
#else
    memcpy(store,persisted,sizeof(store));
#endif
}
static void backend_close(void) {
#ifdef FV_FIDO_ENCRYPTED_TEST
    fv_fido_store_close(&encrypted);fv_fido_fixture_close(&fixture);
#endif
}
static int presence(void *c) { (void)c;return presence_result; }
static uint32_t millis(void *c) { (void)c;return clock_ms; }
int main(int argc, char **argv) {

    uint8_t root[32]={1},id[16]={2},out[FV_FIDO_ENGINE_RESPONSE_SIZE];
#ifdef FV_FIDO_ENCRYPTED_TEST
    const uint16_t algorithms[4]={1,2};
    fv_fido_fixture_init(&fixture,algorithms,2);
    assert(fv_fido_store_initialize(&encrypted,&fixture.vault,true,store)==FV_FIDO_STORE_OK);
    assert(fv_fido_store_engine_key(&encrypted,root)==FV_FIDO_STORE_OK);
    memcpy(id,fixture.vault.config.device_id,16);
#else
    memset(store,0xff,sizeof(store));
#endif
    fv_fido_engine_ops_t ops={.random=random_bytes,.commit=commit,.presence=presence,.millis=millis,
        .verify_user=verify_user,.uv_retries=uv_retries,.cancelled=is_cancelled};
    (void)argv;
    fv_fido_engine_ops_t missing_uv = ops;
    missing_uv.verify_user = NULL;
    assert(!fv_fido_engine_open(store,root,id,&missing_uv));
    assert(fv_fido_engine_open(store,root,id,&ops));
    assert(commits>0);
    const uint8_t info[]={4};
    unsigned initial_commits=commits;
    assert(fv_fido_engine_command(info,sizeof(info),out,1)==1 && out[0]!=0);
    assert(commits==initial_commits);
    if(argc>1) {
        char line[8192];
        while(fgets(line,sizeof(line),stdin)) {
#ifdef FV_FIDO_ENCRYPTED_TEST
            if(!strcmp(line,"change-credential\n")) {
                fv_fido_engine_close();fv_fido_store_close(&encrypted);
                fv_fido_fixture_change(&fixture);
                assert(fv_fido_store_open(&encrypted,&fixture.vault,store)==FV_FIDO_STORE_OK);
                uint8_t new_root[32];
                assert(fv_fido_store_engine_key(&encrypted,new_root)==FV_FIDO_STORE_OK && !memcmp(root,new_root,32));
                assert(fv_fido_engine_open(store,root,id,&ops));puts("ok");fflush(stdout);continue;
            }
#endif
            if(!strcmp(line,"uv-deny\n")) { uv_allowed=false; puts("ok"); fflush(stdout); continue; }
            if(!strcmp(line,"uv-allow\n")) { uv_allowed=true; puts("ok"); fflush(stdout); continue; }
            if(!strcmp(line,"fail-rng\n")) { rng_allowed=false; puts("ok"); fflush(stdout); continue; }
            if(!strcmp(line,"cancel\n")) { cancelled=true; puts("ok"); fflush(stdout); continue; }
            if(!strcmp(line,"uncancel\n")) { cancelled=false; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"time ",5)) { assert(sscanf(line+5,"%u",&clock_ms)==1); puts("ok"); fflush(stdout); continue; }
            if(!strcmp(line,"commits\n")) { printf("%u\n",commits); fflush(stdout); continue; }
            if(!strncmp(line,"deny",4)) { presence_result=2; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"allow",5)) { presence_result=0; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"fail-commit",11)) { commit_allowed=false; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"reopen",6)) {
                commit_allowed=true; rng_allowed=true; cancelled=false;
                fv_fido_engine_close(); reload();
                assert(fv_fido_engine_open(store,root,id,&ops)); puts("ok"); fflush(stdout); continue;
            }
            uint8_t request[FV_FIDO_ENGINE_MESSAGE_SIZE+1]; size_t len=strcspn(line,"\n"); assert(len%2==0 && len/2<=sizeof(request));
            for(size_t i=0;i<len/2;i++) { unsigned v; assert(sscanf(line+i*2,"%2x",&v)==1); request[i]=(uint8_t)v; }
            size_t count=fv_fido_engine_command(request,len/2,out,sizeof(out));
            for(size_t i=0;i<count;i++) printf("%02x",out[i]); puts("");fflush(stdout);
        }
        fv_fido_engine_close(); backend_close(); return 0;
    }

    const uint8_t req[]={6,0xa2,1,1,2,7}; /* ClientPIN getUVRetries */
    size_t n=fv_fido_engine_command(req,sizeof(req),out,sizeof(out));
    printf("response %zu: ",n);for(size_t i=0;i<n;i++)printf("%02x",out[i]);puts("");
    assert(out[0]==0);
    fv_fido_engine_close();
    for (size_t i=0;i<sizeof(store);i++) assert(store[i]==0);
    reload();
    assert(fv_fido_engine_open(store,root,id,&ops));
    fv_fido_engine_close();
    backend_close();
    puts("Pico FIDO built-in UV engine opened, UV retries dispatched, snapshot reopened");
}
