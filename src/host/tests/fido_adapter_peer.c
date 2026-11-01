/* Actual firmware adapter with simulated USB/UI/time and file-backed SD.
 * Included for deterministic inspection of mailbox ownership in this test only. */
#include "../../pico/device/fido_adapter.c"
#include "fido_test_platform.h"
#include <assert.h>
int RAND_bytes(unsigned char *,int);
#include <stdlib.h>
static fv_fido_fixture fixture;
static fv_ui ui;
static fv_access_mode access_mode=FV_ACCESS_FULL;
static uint32_t access_generation=1;
static bool transport_invalidated;
queue_t commands,responses,ui_responses,storage_responses;
char reply[FV_REPLY_BYTES];
atomic_bool fv_ui_maintenance;
static uint32_t clock_ms=100;
static bool command_queued,approval=true,cancel_prompt,wrong_secret,reset_queued,late_cancel,early_cancel,local_flow,local_race;
static bool lock_after_worker,lock_after_report,fragment_test;
static bool timeout_prompt,timeout_before_verify,timeout_after_worker,timeout_after_report;
static unsigned timeout_locks;
static unsigned approvals,verifications;
static unsigned wake_events;
static uint8_t wire[8192];static unsigned wire_size;
uint64_t time_us_64(void){return (uint64_t)clock_ms*1000;}
void tight_loop_contents(void){clock_ms++;}
bool queue_try_add(queue_t *q,const void *data){assert(q==&commands);assert(((const fv_command *)data)->fido);if(command_queued)return false;command_queued=true;return true;}
bool fv_usb_worker_idle(void){return true;}
bool tud_hid_ready(void){return true;}
bool tud_hid_report(uint8_t id,const void *data,uint16_t n){
    assert(!id && n==64 && wire_size+64<=sizeof(wire));memcpy(wire+wire_size,data,64);wire_size+=64;
    if(lock_after_report){lock_after_report=false;fv_fido_lock_session();}
    if(timeout_after_report){
        timeout_after_report=false;clock_ms+=60000;
        assert(fv_session_timeout_poll(clock_ms,access_generation));
    }
    return true;
}
fv_vault *fv_fido_vault(void){return &fixture.vault;}
fv_access_mode fv_fido_access_mode(void){return access_mode;}
bool fv_usb_session_invalidated(void){return transport_invalidated;}
uint32_t fv_usb_session_generation(void){return access_generation;}
void fv_usb_publish_generation(uint32_t generation){access_generation=generation;}
void fv_fido_lock_session(void){fv_usb_publish_generation(access_generation+1);fv_session_timeout_stop();access_mode=FV_ACCESS_LOCKED;fv_fido_close();fv_vault_lock(&fixture.vault);}
void fv_session_timeout_lock(void){timeout_locks++;fv_fido_lock_session();}
bool fv_fido_random(void *ctx,uint8_t *out,size_t n){(void)ctx;return RAND_bytes(out,(int)n)==1;}
uint16_t fv_fido_profile(void){return 2;}
int fv_fido_verify_secret(const uint8_t *secret,size_t n){
    /* Match the device worker's deadline gate before fresh verification. */
    if(fv_session_timeout_poll(clock_ms,access_generation))return FV_VAULT_DENIED;
    if(access_mode!=FV_ACCESS_LOCKED && !fv_access_fido(access_mode))return FV_VAULT_DENIED;
    assert(n==1 && secret[0]==UI_UP);verifications++;
    const uint8_t good[]="fixture secret",bad[]="wrong";
    const uint8_t *value=wrong_secret?bad:good;size_t length=wrong_secret?sizeof(bad)-1:sizeof(good)-1;
    int r=fixture.vault.unlocked?fv_vault_reverify(&fixture.vault,value,length):fv_vault_unlock(&fixture.vault,&fixture.platform,value,length);
    if(r)fv_fido_lock_session();else if(access_mode==FV_ACCESS_LOCKED){access_mode=FV_ACCESS_PASSKEYS_ONLY;fv_usb_publish_generation(access_generation+1);}
    return r;
}
void fv_fido_pump(void){
    (void)fv_session_timeout_poll(clock_ms,access_generation);
    bool opening_prompt=atomic_load(&prompt_state)==1;
    bool dismissing_secret=atomic_load(&prompt_state)==4 && prompt.secret && prompt.approved;
    unsigned before_wake=wake_events;
    fv_fido_ui_poll(&ui);
    if(opening_prompt)assert(wake_events==before_wake+(ui.fido_modal?1u:0u));
    if(opening_prompt && timeout_prompt){timeout_prompt=false;clock_ms+=60000;return;}
    if(dismissing_secret && timeout_before_verify){timeout_before_verify=false;clock_ms+=60000;}
    if(atomic_load(&prompt_state)==2 && !ui.fido_done){
        if(cancel_prompt){atomic_store(&cancel,true);return;}
        if(reset_queued){fv_fido_disconnect();reset_queued=false;return;}
        if(prompt.secret){fv_ui_keypress(&ui,UI_UP);fv_ui_keypress(&ui,UI_SELECT);}
        else {approvals++;fv_ui_keypress(&ui,approval?UI_SELECT:UI_BACK);}
    }
    fv_fido_ui_poll(&ui);
}
fv_usb_response fv_usb_rpc(fv_usb_request request){assert(request.op==FV_USB_LOCK);fv_fido_lock_session();return (fv_usb_response){0};}
void fv_usb_storage_clear_transport(void){}
void fv_device_ui_media_changed(bool unlocked){
    ui.device.unlocked=unlocked;ui.device.mode=access_mode;
    if(!local_flow){ui.pending=false;ui.screen=unlocked?UI_DASHBOARD:UI_HOME;}
}
void fv_device_ui_wake(void){wake_events++;}
bool fv_device_ui_fido_available(void){return !ui.pending && !ui.fido_modal && (ui.screen==UI_HOME || ui.screen==UI_DASHBOARD);}
static void receive(uint32_t cid,uint8_t command,const uint8_t *data,size_t n){
    uint8_t p[64]={cid>>24,cid>>16,cid>>8,cid,command,n>>8,n};size_t copied=n<57?n:57;
    if(copied)memcpy(p+7,data,copied);tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,p,64);
    unsigned seq=0;
    while(copied<n){memset(p+4,0,60);p[4]=seq++;size_t take=n-copied;if(take>59)take=59;memcpy(p+5,data+copied,take);copied+=take;tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,p,64);}
}
static void connect_device(void){
    unsigned before_wake=wake_events;
    transport_invalidated=false;
    fv_fido_disconnect();fv_fido_lock_session();fv_fido_poll();tud_mount_cb();wire_size=0;
    local_flow=false;fv_device_ui_media_changed(false);
    receive(UINT32_MAX,0x86,(const uint8_t *)"nonce123",8);fv_fido_poll();assert(wire_size==64 && wire[18]==1);wire_size=0;
    assert(wake_events==before_wake); /* HID discovery must not light the screen. */
}
static void begin_local_unlock(void){
    local_flow=true;ui.screen=UI_SECRET;ui.flow=UI_FLOW_UNLOCK;ui.job.mode=FV_ACCESS_FILES_READ_ONLY;
    ui.job.secret[0]=UI_DOWN;ui.job.length=1;
}
int main(void){
    const uint16_t alg[4]={1,2};fv_fido_fixture_init(&fixture,alg,2);fv_ui_init(&ui);
    assert(!fv_fido_initialize());fv_fido_poll();connect_device();char line[8192];
    while(fgets(line,sizeof(line),stdin)){
        if(!strcmp(line,"descriptor\n")){const uint8_t *d=tud_hid_descriptor_report_cb(0);for(size_t i=0;i<sizeof(fv_fido_hid_report_descriptor);i++)printf("%02x",d[i]);puts("");}
        else if(!strcmp(line,"reopen\n")){connect_device();puts("ok");}
        else if(!strncmp(line,"mode ",5)){
            unsigned mode;assert(sscanf(line+5,"%u",&mode)==1);
            fv_fido_lock_session();if(mode)fv_fido_fixture_unlock(&fixture);
            access_mode=(fv_access_mode)mode;if(mode)fv_usb_publish_generation(access_generation+1);fv_device_ui_media_changed(mode!=0);
            puts("ok");
        }
        else if(!strcmp(line,"scope\n")){printf("%u %u %u\n",access_mode,fixture.vault.unlocked,engine_open);}
        else if(!strcmp(line,"invalidate\n")){transport_invalidated=true;puts("ok");}
        else if(!strcmp(line,"clear-invalidation\n")){transport_invalidated=false;puts("ok");}
        else if(!strcmp(line,"lock-after-worker\n")){lock_after_worker=true;puts("ok");}
        else if(!strcmp(line,"lock-between-fragments\n")){fragment_test=true;puts("ok");}
        else if(!strcmp(line,"timeout-prompt\n")){timeout_prompt=true;puts("ok");}
        else if(!strcmp(line,"timeout-before-verify\n")){timeout_before_verify=true;puts("ok");}
        else if(!strcmp(line,"timeout-after-worker\n")){timeout_after_worker=true;puts("ok");}
        else if(!strcmp(line,"timeout-between-fragments\n")){fragment_test=true;timeout_after_report=true;puts("ok");}
        else if(!strncmp(line,"timer ",6)){
            unsigned seconds;assert(sscanf(line+6,"%u",&seconds)==1 && fixture.vault.unlocked);
            fv_session_timeout_start(seconds,clock_ms,access_generation);puts("ok");
        }
        else if(!strcmp(line,"remaining\n")){
            (void)fv_session_timeout_poll(clock_ms,access_generation);
            printf("%u\n",fv_session_timeout_remaining());
        }
        else if(!strcmp(line,"timeout-locks\n")){printf("%u\n",timeout_locks);}
        else if(!strcmp(line,"effects\n")){printf("%u %u %u %u %u %u %u\n",fixture.reads,fixture.writes,fixture.syncs,approvals,verifications,fixture.authority.attempts,wake_events);}
        else if(!strcmp(line,"local-readonly-check\n")){
            assert(access_mode==FV_ACCESS_FILES_READ_ONLY && fixture.vault.unlocked && !engine_open);
            uint16_t index=0,count=0;fv_passkey_t entry={0};
            assert(fv_fido_policy_get()==0 && fv_fido_policy_set(0)!=0);
            assert(fv_fido_manage(false,&index,&count,&entry)!=0 && fv_fido_manage(true,&index,&count,&entry)!=0);
            assert(fv_fido_initialize()!=0 && !ensure_engine() && !local(NULL) && !verify(NULL,NULL));
            assert(!engine_open && fixture.vault.unlocked);puts("ok");
        }
        else if(!strcmp(line,"local-begin\n")){
            begin_local_unlock();puts("ok");
        }
        else if(!strcmp(line,"local-race\n")){local_race=true;puts("ok");}
        else if(!strcmp(line,"local-preserved\n")){
            assert(local_flow && ui.screen==UI_SECRET && ui.flow==UI_FLOW_UNLOCK && ui.job.mode==FV_ACCESS_FILES_READ_ONLY);
            assert(ui.job.length==1 && ui.job.secret[0]==UI_DOWN && !ui.fido_modal);puts("ok");
        }
        else if(!strcmp(line,"local-end\n")){local_flow=false;fv_ui_wipe(&ui.job,sizeof(ui.job));fv_device_ui_media_changed(fixture.vault.unlocked);puts("ok");}
        else if(!strncmp(line,"policy ",7)){unsigned mode;assert(sscanf(line+7,"%u",&mode)==1);assert(fv_fido_policy_set((uint8_t)mode)==0);puts("ok");}
        else if(!strncmp(line,"local-list ",11)){
            unsigned position;assert(sscanf(line+11,"%u",&position)==1);uint16_t index=position,count=0;fv_passkey_t entry={0};
            if(fv_fido_manage(false,&index,&count,&entry))puts("denied");
            else {printf("%u %u ",count,index);for(unsigned i=0;i<42;i++)printf("%02x",entry.id[i]);puts("");}
        }
        else if(!strncmp(line,"local-delete ",13)){
            fv_passkey_t entry={0};uint16_t index=0,count=0;
            assert(strcspn(line+13,"\n")==84);
            for(unsigned i=0;i<42;i++){unsigned b;assert(sscanf(line+13+i*2,"%2x",&b)==1);entry.id[i]=b;}
            puts(fv_fido_manage(true,&index,&count,&entry)?"denied":"ok");
        }
        else if(!strcmp(line,"policy-get\n")){printf("%u\n",fv_fido_policy_get());}
        else if(!strcmp(line,"policy-fail\n")){fixture.fail_write=fixture.writes+1;assert(fv_fido_policy_set(1)!=0);fv_fido_fixture_io_reset(&fixture);assert(fv_fido_policy_get()==0);puts("ok");}
        else if(!strcmp(line,"policy-invalid\n")){assert(fv_fido_policy_set(2)!=0);puts("ok");}
        else if(!strcmp(line,"policy-locked\n")){assert(!fixture.vault.unlocked && fv_fido_policy_set(1)!=0);puts("ok");}
        else if(!strcmp(line,"engine-close\n")){fv_fido_lock_session();fv_device_ui_media_changed(false);puts("ok");}
        else if(!strcmp(line,"deny\n")){approval=false;puts("ok");}
        else if(!strcmp(line,"allow\n")){approval=true;puts("ok");}
        else if(!strcmp(line,"cancel-late\n")){late_cancel=true;puts("ok");}
        else if(!strcmp(line,"cancel-early\n")){early_cancel=true;puts("ok");}
        else if(!strcmp(line,"locked\n")){assert(!fixture.vault.unlocked && !engine_open);puts("ok");}
        else if(!strcmp(line,"cancel\n")){cancel_prompt=true;puts("ok");}
        else if(!strcmp(line,"uncancel\n")){cancel_prompt=false;puts("ok");}
        else if(!strcmp(line,"wrong\n")){wrong_secret=true;puts("ok");}
        else if(!strcmp(line,"correct\n")){wrong_secret=false;puts("ok");}
        else if(!strncmp(line,"time ",5)){assert(sscanf(line+5,"%u",&clock_ms)==1);puts("ok");}
        else if(!strncmp(line,"advance ",8)){unsigned ms;assert(sscanf(line+8,"%u",&ms)==1);clock_ms+=ms;puts("ok");}
        else if(!strcmp(line,"counts\n")){printf("%u %u %u\n",approvals,verifications,fixture.authority.attempts);}
        else if(!strcmp(line,"attempt-limit-off\n")){
            static const uint8_t secret[]="fixture secret";
            fv_fido_lock_session();
            assert(fv_vault_change_credential(&fixture.vault,&fixture.platform,secret,sizeof(secret)-1,
                secret,sizeof(secret)-1,1,2,(fv_auth_policy){0,FV_LIMIT_NONE,300},true)==FV_VAULT_OK);
            fv_device_ui_media_changed(false);puts("ok");
        }
        else if(!strcmp(line,"authority-sequence\n")){printf("%llu\n",(unsigned long long)fixture.authority.sequence);}
        else {
            size_t len=strcspn(line,"\n");assert(!(len%2) && len/2<=4096);uint8_t request[4096];
            for(size_t i=0;i<len/2;i++){unsigned b;assert(sscanf(line+2*i,"%2x",&b)==1);request[i]=b;}
            unsigned before_wake=wake_events;
            receive(1,0x90,request,len/2);fv_fido_poll();
            assert(wake_events==before_wake); /* Only an actual modal wakes the screen. */
            if(command_queued){
                command_queued=false;
                if(local_race){begin_local_unlock();local_race=false;}
                if(early_cancel){receive(1,0x91,NULL,0);early_cancel=false;}
                fv_fido_execute();
                if(lock_after_worker){fv_fido_lock_session();lock_after_worker=false;}
                if(timeout_after_worker){
                    timeout_after_worker=false;clock_ms+=60000;
                    assert(fv_session_timeout_poll(clock_ms,access_generation));
                }
                if(late_cancel){receive(1,0x91,NULL,0);late_cancel=false;}
                if(fragment_test && !timeout_after_report)lock_after_report=true;
                fv_fido_poll();
            }
            while(hid.transmitting)fv_fido_poll();assert(wire_size>=64 && wire[4]==0x90);
            unsigned total=((unsigned)wire[5]<<8)|wire[6],written=0;
            if(fragment_test){
                assert(!lock_after_report && !timeout_after_report && wire_size==64 && total>57 && !hid.transmitting);
                for(unsigned i=0;i<sizeof(hid.tx);i++)assert(hid.tx[i]==0);
                assert(access_mode==FV_ACCESS_LOCKED && !fixture.vault.unlocked && !engine_open);
                fragment_test=false;wire_size=0;puts("fragment-blocked");fflush(stdout);continue;
            }
            for(unsigned off=0;off<wire_size && written<total;off+=64){unsigned start=off?5:7,take=64-start;if(take>total-written)take=total-written;
                for(unsigned i=0;i<take;i++)printf("%02x",wire[off+start+i]);written+=take;}
            assert(written==total);puts("");wire_size=0;
        }fflush(stdout);
    }
    fv_fido_close();fv_fido_fixture_close(&fixture);return 0;
}
