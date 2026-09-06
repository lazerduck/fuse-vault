#include "fuse_vault/fido_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/rand.h>
static uint8_t store[FV_FIDO_STORE_BYTES];
static uint8_t persisted[FV_FIDO_STORE_BYTES];
static bool verify_user(void *c, const uint8_t *rp) { (void)c; (void)rp; return true; }
static unsigned commits;
static bool commit_allowed = true;
static int presence_result;
static bool random_bytes(void *c,uint8_t *out,size_t n) { (void)c; return RAND_bytes(out,(int)n)==1; }
static bool commit(void *c,const uint8_t *in,size_t n) { (void)c; assert(n==sizeof(store));if (!commit_allowed) return false;memcpy(persisted,in,n);++commits;return true; }
static int presence(void *c) { (void)c;return presence_result; }
static uint32_t millis(void *c) { (void)c;return 1; }
int main(int argc, char **argv) {

    uint8_t root[32]={1},id[16]={2},out[2048];
    memset(store,0xff,sizeof(store));
    fv_fido_engine_ops_t ops={.random=random_bytes,.commit=commit,.presence=presence,.millis=millis};
    if (argc > 2 && !strcmp(argv[2], "uv")) ops.verify_user = verify_user;
    assert(fv_fido_engine_open(store,root,id,&ops));
    assert(commits>0);
    if(argc>1) {
        char line[8192];
        while(fgets(line,sizeof(line),stdin)) {
            if(!strncmp(line,"deny",4)) { presence_result=2; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"allow",5)) { presence_result=0; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"fail-commit",11)) { commit_allowed=false; puts("ok"); fflush(stdout); continue; }
            if(!strncmp(line,"reopen",6)) {
                commit_allowed=true;
                fv_fido_engine_close(); memcpy(store,persisted,sizeof(store));
                assert(fv_fido_engine_open(store,root,id,&ops)); puts("ok"); fflush(stdout); continue;
            }
            uint8_t request[2048]; size_t len=strcspn(line,"\n"); assert(len%2==0 && len/2<=sizeof(request));
            for(size_t i=0;i<len/2;i++) { unsigned v; assert(sscanf(line+i*2,"%2x",&v)==1); request[i]=(uint8_t)v; }
            size_t count=fv_fido_engine_command(request,len/2,out,sizeof(out));
            for(size_t i=0;i<count;i++) printf("%02x",out[i]); puts("");fflush(stdout);
        }
        fv_fido_engine_close(); return 0;
    }

    const uint8_t req[]={6,0xa2,1,1,2,1}; /* ClientPIN getRetries */
    size_t n=fv_fido_engine_command(req,sizeof(req),out,sizeof(out));
    printf("response %zu: ",n);for(size_t i=0;i<n;i++)printf("%02x",out[i]);puts("");
    assert(out[0]==0);
    fv_fido_engine_close();
    memcpy(store,persisted,sizeof(store));
    assert(fv_fido_engine_open(store,root,id,&ops));
    fv_fido_engine_close();
    puts("Pico FIDO engine opened, ClientPIN dispatched, snapshot reopened");
}
