#include "fuse_vault/fido_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/rand.h>
/* The journal peer deliberately has no image buffer. */
static uint8_t *store;
#include "fido_test_platform.h"
#include "fuse_vault/fido_journal.h"
static fv_fido_fixture fixture;
static fv_fido_journal encrypted;
static fv_fido_store legacy_store;
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
    (void)c; (void)in; (void)n;
    /* Fail actual media I/O rather than bypassing the encrypted store callback. */
    if(store){if(!commit_allowed || fv_fido_store_commit(&legacy_store,store))return false;++commits;return true;}
    if (!commit_allowed) fixture.fail_write=fixture.writes+1;
    if (!fv_fido_journal_commit(&encrypted)) return false;
    ++commits;return true;
}
static bool commit_disk(void *ctx){return commit(ctx,NULL,0);}
static void reload(void) {
    fv_fido_journal_close(&encrypted);
    fv_vault_lock(&fixture.vault);
    fv_fido_fixture_io_reset(&fixture);
    fv_fido_fixture_unlock(&fixture);
    assert(fv_fido_journal_open(&encrypted,&fixture.vault)==0);
}
static void backend_close(void) {
    free(store);store=NULL;
    fv_fido_journal_close(&encrypted);fv_fido_fixture_close(&fixture);
}
static int presence(void *c) { (void)c;return presence_result; }
static uint32_t millis(void *c) { (void)c;return clock_ms; }
int main(int argc, char **argv) {

    uint8_t root[32]={1},id[16]={2},out[FV_FIDO_ENGINE_RESPONSE_SIZE];
    const uint16_t algorithms[4]={1,2};
    fv_fido_fixture_init(&fixture,algorithms,2);
    assert(fv_fido_journal_initialize(&encrypted,&fixture.vault,true)==0);
    assert(fv_fido_journal_key(&encrypted,root)==0);
    memcpy(id,fixture.vault.config.device_id,16);
    fv_fido_engine_ops_t ops={.random=random_bytes,.commit=commit,.storage_context=&encrypted,.storage_bytes=FV_FIDO_DISK_BYTES,
        .storage_read=fv_fido_journal_read,.storage_write=fv_fido_journal_write,
        .storage_commit=commit_disk,.storage_reset=fv_fido_journal_reset,.presence=presence,.millis=millis,
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
            if(!strcmp(line,"change-credential\n")) {
                fv_fido_engine_close();fv_fido_journal_close(&encrypted);
                fv_fido_fixture_change(&fixture);
                assert(fv_fido_journal_open(&encrypted,&fixture.vault)==0);
                uint8_t new_root[32];
                assert(fv_fido_journal_key(&encrypted,new_root)==0 && !memcmp(root,new_root,32));
                assert(fv_fido_engine_open(store,root,id,&ops));puts("ok");fflush(stdout);continue;
            }
            if(!strcmp(line,"make-legacy\n")) {
                fv_fido_engine_close();fv_fido_journal_close(&encrypted);
                uint8_t empty[512];memset(empty,0xa5,512);
                for(unsigned i=16;i<2064;i++)assert(fixture.device.ops->write(&fixture.device,i,1,empty)==FV_BLOCK_OK);
                store=malloc(FV_FIDO_STORE_BYTES);assert(store);
                assert(!fv_fido_store_initialize(&legacy_store,&fixture.vault,true,store));
                ops.storage_read=NULL;ops.storage_write=NULL;ops.storage_commit=NULL;ops.storage_reset=NULL;
                assert(fv_fido_engine_open(store,root,id,&ops));puts("ok");fflush(stdout);continue;
            }
            if(!strcmp(line,"migrate\n")) {
                assert(store);fv_fido_engine_close();free(store);store=NULL;fv_fido_store_close(&legacy_store);
                assert(!fv_fido_journal_open(&encrypted,&fixture.vault));
                ops.storage_read=fv_fido_journal_read;ops.storage_write=fv_fido_journal_write;
                ops.storage_commit=commit_disk;ops.storage_reset=fv_fido_journal_reset;
                assert(fv_fido_engine_open(NULL,root,id,&ops));puts("ok");fflush(stdout);continue;
            }
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

    reload();
    assert(fv_fido_engine_open(store,root,id,&ops));
    fv_fido_engine_close();
    backend_close();
    puts("Pico FIDO built-in UV engine opened, UV retries dispatched, snapshot reopened");
}
