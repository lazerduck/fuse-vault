#define _GNU_SOURCE
#include "host_services.h"
#include "fuse_vault/authentication_coordinator.h"
#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/provisioning_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(EXIT_FAILURE);}}while(0)
static fv_app_t app_for_provision(void){fv_app_t a;fv_app_init(&a,false,0,FV_ENTRY_METHOD_DIRECTIONS);a.state=FV_STATE_PROVISIONING;a.setup_secret_entry.method=FV_ENTRY_METHOD_DIRECTIONS;a.setup_secret_entry.state.directions.length=6;const uint8_t v[6]={0,1,2,3,0,1};memcpy(a.setup_secret_entry.state.directions.values,v,6);return a;}
static void cleanup(const char*d){const char*names[]={"device-secret.0","device-secret.1","device-secret-active.0","device-secret-active.1","device-secret-revoked.0","device-secret-revoked.1","security-state.0","security-state.1","security-journal.bin","vault-media.bin"};char p[4096];for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){int n=snprintf(p,sizeof(p),"%s/%s",d,names[i]);if(n>0&&(size_t)n<sizeof(p))(void)unlink(p);}(void)rmdir(d);}
int main(void){char dir[]="/tmp/fuse-vault-persistent-XXXXXX";CHECK(mkdtemp(dir));fv_platform_services_t first;fv_host_services_context_t c1;CHECK(fv_host_services_init(&first,&c1,dir));fv_app_t provision=app_for_provision();fv_setup_provision_workspace_t pw;const fv_credential_costs_t costs={1,1};CHECK(fv_setup_provision(&provision,&first,&costs,&pw)==FV_SETUP_PROVISION_OK);
 fv_platform_services_t restarted;fv_host_services_context_t c2;CHECK(fv_host_services_init(&restarted,&c2,dir));fv_security_state_t state;CHECK(restarted.ops->load_security_state(&restarted,&state)==FV_PERSIST_OK&&state.provisioned);fv_entry_method_t method;CHECK(fv_boot_recover_entry_method(&restarted,&method)==FV_BOOT_RECOVERY_OK&&method==FV_ENTRY_METHOD_DIRECTIONS);fv_app_t auth;fv_app_init(&auth,true,1u,method);auth.state=FV_STATE_VAULT_AUTHENTICATING;auth.secret_entry=provision.setup_secret_entry;fv_authentication_session_t session;fv_authentication_workspace_t aw;CHECK(fv_authenticate(&auth,&restarted,&session,&aw)==FV_AUTHENTICATE_OK);CHECK(session.vmk_valid);fv_authentication_session_clear(&session);cleanup(dir);puts("Persistent provision/restart/authentication test passed.");return EXIT_SUCCESS;}
