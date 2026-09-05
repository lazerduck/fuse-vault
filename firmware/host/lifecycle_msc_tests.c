#define _GNU_SOURCE
#include "host_services.h"
#include "fuse_vault/authentication_coordinator.h"
#include "fuse_vault/block_slice.h"
#include "fuse_vault/encrypted_block.h"
#include "fuse_vault/provisioning_coordinator.h"
#include "fuse_vault/virtual_msc.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)

static void set_secret(fv_secret_entry_t *entry, fv_entry_method_t method,
                       bool correct) {
    memset(entry, 0, sizeof(*entry)); entry->method = method;
    if (method == FV_ENTRY_METHOD_WHEELS) {
        entry->state.wheels.values[0]=12u; entry->state.wheels.values[1]=34u;
        entry->state.wheels.values[2]=(uint8_t)(correct?56u:57u);
    } else if (method == FV_ENTRY_METHOD_DIRECTIONS) {
        static const uint8_t v[6]={0u,1u,2u,3u,0u,1u};
        entry->state.directions.length=6u; memcpy(entry->state.directions.values,v,6u);
        if(!correct) entry->state.directions.values[5]^=1u;
    } else if (method == FV_ENTRY_METHOD_KEYPAD) {
        entry->state.keypad.length=4u; entry->state.keypad.digits[0]=1u;
        entry->state.keypad.digits[1]=9u; entry->state.keypad.digits[2]=8u;
        entry->state.keypad.digits[3]=(uint8_t)(correct?4u:5u);
    } else {
        entry->state.word_list.words[0]=3u; entry->state.word_list.words[1]=17u;
        entry->state.word_list.words[2]=31u;
        entry->state.word_list.words[3]=(uint16_t)(correct?63u:64u);
    }
}
static fv_app_t provisioning_app(fv_entry_method_t method) {
    fv_app_t app; fv_app_init(&app,false,0u,method); app.state=FV_STATE_PROVISIONING;
    set_secret(&app.setup_secret_entry,method,true); return app;
}
static fv_authenticate_result_t authenticate(fv_platform_services_t *services,
    fv_entry_method_t method, uint8_t attempts, bool correct,
    fv_authentication_session_t *session) {
    fv_app_t app; fv_app_init(&app,true,attempts,method);
    app.state=FV_STATE_VAULT_AUTHENTICATING; set_secret(&app.secret_entry,method,correct);
    fv_authentication_workspace_t workspace;
    return fv_authenticate(&app,services,session,&workspace);
}
static bool contains(const uint8_t *haystack,size_t hn,const uint8_t *needle,size_t nn){
    if(nn==0u||hn<nn)return false;
    for(size_t i=0u;i<=hn-nn;++i) {
        if(memcmp(haystack+i,needle,nn)==0)return true;
    }
    return false;
}
static bool encrypted_rng(void *context,uint8_t *output,size_t length){
    fv_platform_services_t *services=context;
    return services->ops->random_fill(services,output,length);
}
static void path(char out[4096],const char*d,const char*n){CHECK(snprintf(out,4096,"%s/%s",d,n)>0);}
static void cleanup(const char*d){const char*names[]={"device-secret.0","device-secret.1",
 "device-secret-active.0","device-secret-active.1","device-secret-revoked.0",
 "device-secret-revoked.1","security-state.0","security-state.1",
 "security-journal.bin","vault-media.bin"};
 char p[4096];for(size_t i=0u;i<sizeof(names)/sizeof(names[0]);++i){path(p,d,names[i]);(void)unlink(p);}(void)rmdir(d);}

static void full_lifecycle(fv_entry_method_t method) {
    char directory[]="/tmp/fuse-vault-lifecycle-XXXXXX"; CHECK(mkdtemp(directory));
    fv_platform_services_t services;fv_host_services_context_t context;
    CHECK(fv_host_services_init(&services,&context,directory));
    fv_app_t provision=provisioning_app(method);fv_setup_provision_workspace_t pw;
    const fv_credential_costs_t costs={1u,1u};
    CHECK(fv_setup_provision(&provision,&services,&costs,&pw)==FV_SETUP_PROVISION_OK);

    fv_platform_services_t restarted;fv_host_services_context_t restarted_context;
    CHECK(fv_host_services_init(&restarted,&restarted_context,directory));
    fv_security_state_t state;CHECK(restarted.ops->load_security_state(&restarted,&state)==FV_PERSIST_OK);
    state.sequence++;state.failed_attempts=1u;CHECK(restarted.ops->store_security_state(&restarted,&state)==FV_PERSIST_OK);
    fv_authentication_session_t rejected;CHECK(authenticate(&restarted,method,1u,false,&rejected)==FV_AUTHENTICATE_REJECTED);

    fv_platform_services_t again;fv_host_services_context_t again_context;
    CHECK(fv_host_services_init(&again,&again_context,directory));
    CHECK(again.ops->load_security_state(&again,&state)==FV_PERSIST_OK&&state.failed_attempts==1u);
    state.sequence++;state.failed_attempts=2u;CHECK(again.ops->store_security_state(&again,&state)==FV_PERSIST_OK);
    fv_authentication_session_t session;CHECK(authenticate(&again,method,2u,true,&session)==FV_AUTHENTICATE_OK&&session.vmk_valid);
    state.sequence++;state.failed_attempts=0u;CHECK(again.ops->store_security_state(&again,&state)==FV_PERSIST_OK);

    fv_vault_header_t header;CHECK(again.ops->load_vault_header(&again,&header)==FV_PERSIST_OK);
    fv_device_secret_t roots;CHECK(again.ops->read_device_secret(&again,&roots)==FV_PERSIST_OK);
    fv_volume_master_key_t raw_search_vmk=session.vmk;
    fv_block_slice_t data;CHECK(fv_block_slice_init(&data,&again_context.vault_device,2u,64u));
    fv_encrypted_block_t encrypted;CHECK(fv_encrypted_block_init(&encrypted,&data.interface,&session.vmk,header.vault_id,
        encrypted_rng,&again));
    fv_virtual_msc_t msc;fv_virtual_msc_init(&msc);uint8_t plain[512],output[512];
    memset(plain,0,sizeof(plain));memcpy(plain,"stage-3 supplied plaintext marker",33u);
    CHECK(fv_virtual_msc_read10(&msc,0u,1u,output)==FV_MSC_NOT_READY);
    CHECK(fv_virtual_msc_attach(&msc,&encrypted.interface));
    CHECK(fv_virtual_msc_write10(&msc,3u,1u,plain)==FV_MSC_OK);
    CHECK(fv_virtual_msc_synchronize_cache(&msc)==FV_MSC_OK);
    CHECK(fv_virtual_msc_read10(&msc,3u,1u,output)==FV_MSC_OK&&memcmp(plain,output,512u)==0);
    CHECK(fv_virtual_msc_start_stop(&msc,false,true)==FV_MSC_OK);
    fv_virtual_msc_detach(&msc);fv_encrypted_block_lock(&encrypted);
    fv_authentication_session_clear(&session);
    CHECK(fv_virtual_msc_read10(&msc,3u,1u,output)==FV_MSC_NOT_READY);

    char media_path[4096];path(media_path,directory,"vault-media.bin");int fd=open(media_path,O_RDONLY);CHECK(fd>=0);
    uint8_t raw[66u*512u];CHECK(read(fd,raw,sizeof(raw))==(ssize_t)sizeof(raw));CHECK(close(fd)==0);
    CHECK(!contains(raw,sizeof(raw),plain,sizeof(plain)));
    CHECK(!contains(raw,sizeof(raw),roots.device_secret,sizeof(roots.device_secret)));
    CHECK(!contains(raw,sizeof(raw),raw_search_vmk.bytes,sizeof(raw_search_vmk.bytes)));
    memset(&roots,0,sizeof(roots));memset(&raw_search_vmk,0,sizeof(raw_search_vmk));

    fv_authentication_session_t resumed;CHECK(authenticate(&again,method,1u,true,&resumed)==FV_AUTHENTICATE_OK);
    CHECK(fv_encrypted_block_init(&encrypted,&data.interface,&resumed.vmk,header.vault_id,
        encrypted_rng,&again));
    CHECK(fv_virtual_msc_attach(&msc,&encrypted.interface));
    CHECK(fv_virtual_msc_read10(&msc,3u,1u,output)==FV_MSC_OK&&memcmp(plain,output,512u)==0);
    CHECK(unlink(media_path)==0);CHECK(fv_virtual_msc_read10(&msc,3u,1u,output)==FV_MSC_NOT_READY);
    fv_virtual_msc_block_requests(&msc);fv_virtual_msc_detach(&msc);fv_encrypted_block_fault(&encrypted);
    fv_authentication_session_clear(&resumed);cleanup(directory);
}

static void tenth_attempt_and_replacement(void) {
    char directory[]="/tmp/fuse-vault-revoke-XXXXXX";CHECK(mkdtemp(directory));
    fv_platform_services_t services;fv_host_services_context_t context;CHECK(fv_host_services_init(&services,&context,directory));
    fv_app_t provision=provisioning_app(FV_ENTRY_METHOD_KEYPAD);fv_setup_provision_workspace_t pw;
    const fv_credential_costs_t costs={1u,1u};CHECK(fv_setup_provision(&provision,&services,&costs,&pw)==FV_SETUP_PROVISION_OK);
    fv_security_state_t state;CHECK(services.ops->load_security_state(&services,&state)==FV_PERSIST_OK);
    for(uint8_t attempt=1u;attempt<=10u;++attempt){state.sequence++;state.failed_attempts=attempt;
        CHECK(services.ops->store_security_state(&services,&state)==FV_PERSIST_OK);
        if(attempt<10u){fv_authentication_session_t s;CHECK(authenticate(&services,FV_ENTRY_METHOD_KEYPAD,attempt,false,&s)==FV_AUTHENTICATE_REJECTED);}}
    CHECK(services.ops->revoke_device_secret(&services)==FV_PERSIST_OK);
    char media[4096];path(media,directory,"vault-media.bin");CHECK(unlink(media)==0);
    fv_platform_services_t restarted;fv_host_services_context_t c2;CHECK(fv_host_services_init(&restarted,&c2,directory));
    fv_device_secret_status_t status;CHECK(restarted.ops->device_secret_status(&restarted,&status)==FV_PERSIST_OK&&status==FV_DEVICE_SECRET_REVOKED);
    fv_authentication_session_t s;CHECK(authenticate(&restarted,FV_ENTRY_METHOD_KEYPAD,10u,true,&s)==FV_AUTHENTICATE_FATAL);
    cleanup(directory);
}

int main(void){for(fv_entry_method_t m=FV_ENTRY_METHOD_WHEELS;m<FV_ENTRY_METHOD_COUNT;m=(fv_entry_method_t)(m+1))full_lifecycle(m);
 tenth_attempt_and_replacement();puts("Stage 3 lifecycle and virtual MSC tests passed.");return EXIT_SUCCESS;}
