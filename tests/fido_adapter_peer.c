/* Actual firmware adapter with simulated USB/UI/time and file-backed SD.
 * Included for deterministic inspection of mailbox ownership in this test only. */
#include "../firmware/security/fido_adapter.c"
#include "fido_test_platform.h"
#include <assert.h>
int RAND_bytes(unsigned char *,int);
#include <stdlib.h>
static fv_fido_fixture fixture;
static fv_ui ui;
queue_t commands,responses,ui_responses,storage_responses;
char reply[FV_REPLY_BYTES];
atomic_bool fv_ui_maintenance;
static uint32_t clock_ms=100;
static bool command_queued,approval=true,cancel_prompt,wrong_secret,reset_queued,late_cancel;
static unsigned approvals,verifications;
static uint8_t wire[8192];static unsigned wire_size;
uint64_t time_us_64(void){return (uint64_t)clock_ms*1000;}
void tight_loop_contents(void){clock_ms++;}
bool queue_try_add(queue_t *q,const void *data){assert(q==&commands);assert(((const fv_command *)data)->fido);if(command_queued)return false;command_queued=true;return true;}
bool fv_usb_worker_idle(void){return true;}
bool tud_hid_ready(void){return true;}
bool tud_hid_report(uint8_t id,const void *data,uint16_t n){
    assert(!id && n==64 && wire_size+64<=sizeof(wire));memcpy(wire+wire_size,data,64);wire_size+=64;return true;
}
fv_vault *fv_fido_vault(void){return &fixture.vault;}
bool fv_fido_random(void *ctx,uint8_t *out,size_t n){(void)ctx;return RAND_bytes(out,(int)n)==1;}
uint16_t fv_fido_profile(void){return 2;}
int fv_fido_verify_secret(const uint8_t *secret,size_t n){
    assert(n==1 && secret[0]==UI_UP);verifications++;
    const uint8_t good[]="fixture secret",bad[]="wrong";
    const uint8_t *value=wrong_secret?bad:good;size_t length=wrong_secret?sizeof(bad)-1:sizeof(good)-1;
    int r=fixture.vault.unlocked?fv_vault_reverify(&fixture.vault,value,length):fv_vault_unlock(&fixture.vault,&fixture.platform,value,length);
    if(r)fv_fido_close();return r;
}
void fv_fido_pump(void){
    fv_fido_ui_poll(&ui);
    if(atomic_load(&prompt_state)==2 && !ui.fido_done){
        if(cancel_prompt){atomic_store(&cancel,true);return;}
        if(reset_queued){fv_fido_disconnect();reset_queued=false;return;}
        if(prompt.secret){fv_ui_keypress(&ui,UI_UP);fv_ui_keypress(&ui,UI_SELECT);}
        else {approvals++;fv_ui_keypress(&ui,approval?UI_SELECT:UI_BACK);}
    }
    fv_fido_ui_poll(&ui);
}
fv_usb_response fv_usb_rpc(fv_usb_request request){assert(request.op==FV_USB_LOCK);fv_fido_close();fv_vault_lock(&fixture.vault);return (fv_usb_response){0};}
void fv_usb_storage_clear_transport(void){}
void fv_device_ui_media_changed(bool unlocked){(void)unlocked;}
static void receive(uint32_t cid,uint8_t command,const uint8_t *data,size_t n){
    uint8_t p[64]={cid>>24,cid>>16,cid>>8,cid,command,n>>8,n};size_t copied=n<57?n:57;
    if(copied)memcpy(p+7,data,copied);tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,p,64);
    unsigned seq=0;
    while(copied<n){memset(p+4,0,60);p[4]=seq++;size_t take=n-copied;if(take>59)take=59;memcpy(p+5,data+copied,take);copied+=take;tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,p,64);}
}
static void connect_device(void){
    fv_fido_disconnect();fv_fido_close();fv_vault_lock(&fixture.vault);fv_fido_poll();tud_mount_cb();wire_size=0;
    receive(UINT32_MAX,0x86,(const uint8_t *)"nonce123",8);fv_fido_poll();assert(wire_size==64 && wire[18]==1);wire_size=0;
}
int main(void){
    const uint16_t alg[4]={1,2};fv_fido_fixture_init(&fixture,alg,2);fv_ui_init(&ui);
    assert(!fv_fido_initialize());fv_fido_poll();connect_device();char line[8192];
    while(fgets(line,sizeof(line),stdin)){
        if(!strcmp(line,"descriptor\n")){const uint8_t *d=tud_hid_descriptor_report_cb(0);for(size_t i=0;i<sizeof(fv_fido_hid_report_descriptor);i++)printf("%02x",d[i]);puts("");}
        else if(!strcmp(line,"reopen\n")){connect_device();puts("ok");}
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
        else if(!strcmp(line,"engine-close\n")){fv_fido_close();fv_vault_lock(&fixture.vault);puts("ok");}
        else if(!strcmp(line,"deny\n")){approval=false;puts("ok");}
        else if(!strcmp(line,"allow\n")){approval=true;puts("ok");}
        else if(!strcmp(line,"cancel-late\n")){late_cancel=true;puts("ok");}
        else if(!strcmp(line,"locked\n")){assert(!fixture.vault.unlocked && !engine_open);puts("ok");}
        else if(!strcmp(line,"cancel\n")){cancel_prompt=true;puts("ok");}
        else if(!strcmp(line,"uncancel\n")){cancel_prompt=false;puts("ok");}
        else if(!strcmp(line,"wrong\n")){wrong_secret=true;puts("ok");}
        else if(!strcmp(line,"correct\n")){wrong_secret=false;puts("ok");}
        else if(!strncmp(line,"time ",5)){assert(sscanf(line+5,"%u",&clock_ms)==1);puts("ok");}
        else if(!strcmp(line,"counts\n")){printf("%u %u %u\n",approvals,verifications,fixture.authority.attempts);}
        else {
            size_t len=strcspn(line,"\n");assert(!(len%2) && len/2<=4096);uint8_t request[4096];
            for(size_t i=0;i<len/2;i++){unsigned b;assert(sscanf(line+2*i,"%2x",&b)==1);request[i]=b;}
            receive(1,0x90,request,len/2);fv_fido_poll();assert(command_queued);command_queued=false;
            fv_fido_execute();if(late_cancel){receive(1,0x91,NULL,0);late_cancel=false;}fv_fido_poll();while(hid.transmitting)fv_fido_poll();assert(wire_size>=64 && wire[4]==0x90);
            unsigned total=((unsigned)wire[5]<<8)|wire[6],written=0;
            for(unsigned off=0;off<wire_size && written<total;off+=64){unsigned start=off?5:7,take=64-start;if(take>total-written)take=total-written;
                for(unsigned i=0;i<take;i++)printf("%02x",wire[off+start+i]);written+=take;}
            assert(written==total);puts("");wire_size=0;
        }fflush(stdout);
    }
    fv_fido_close();fv_fido_fixture_close(&fixture);return 0;
}
