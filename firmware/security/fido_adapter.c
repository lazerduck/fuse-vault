#include "fido_adapter.h"
#include "fuse_vault/fido_hid_descriptor.h"
#include "bringup.h"
#include "device_ui_adapter.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "fuse_vault/fido_hid.h"
#include "fuse_vault/fido_engine.h"
#include "fuse_vault/fido_store.h"
#include "fuse_vault/fido_verification.h"
#include "cbor.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
/* HID is core 0 only. The worker receives immutable rx until done is acquired.
 * Prompt fields have exactly one owner at each atomic state transition. */
static fv_fido_hid_t hid;
static atomic_bool busy,cancel,disconnected;
static atomic_uint done,enumerated_at;
static atomic_bool enumerated;
static uint8_t response[FV_FIDO_ENGINE_RESPONSE_SIZE];
static atomic_bool prompt_abort;
static atomic_uint prompt_state; /* 0 idle, 1 publish, 2 UI, 3 answer, 4 dismiss */
static struct {bool secret,approved;uint16_t profile;uint8_t value[64],length;char label[128];} prompt;
static bool discard,initialized;
static bool completed_unlocked;
static uint32_t keepalive_at;
static fv_fido_store store;
static fv_fido_verification_t verification;
static uint8_t image[FV_FIDO_STORE_BYTES];
static bool engine_open,executing,invalidated;
static char request_label[128];
static uint8_t uv_policy;
static uint32_t now(void *context){(void)context;return (uint32_t)(time_us_64()/1000);}
bool fv_fido_busy(void){return atomic_load(&busy);}
bool fv_fido_disk_available(void){unsigned s=atomic_load(&prompt_state);return s==1 || s==2;}
bool fv_fido_cancelled(void *ctx){(void)ctx;return atomic_load(&cancel) || invalidated;}
void fv_fido_disconnect(void){atomic_store(&cancel,true);atomic_store(&disconnected,true);atomic_store(&enumerated,false);}
void tud_mount_cb(void){atomic_store(&enumerated_at,now(NULL));atomic_store(&enumerated,true);}
void fv_fido_close(void){
    fv_fido_verification_clear(&verification);
    if(executing){invalidated=true;return;}
    if(engine_open)fv_fido_engine_close();
    engine_open=false;fv_fido_store_close(&store);fv_ui_wipe(image,sizeof(image));
}
void fv_fido_unlocked(void){fv_fido_verification_begin(&verification,now(NULL));}
void fv_fido_ui_poll(fv_ui *ui){
    unsigned state=atomic_load(&prompt_state);
    if(state==1){fv_ui_fido_begin(ui,prompt.secret,prompt.profile,prompt.label);atomic_store(&prompt_state,2);}
    else if(state==2 && (ui->fido_done || atomic_load(&prompt_abort))){
        prompt.approved=ui->fido_approved && !atomic_load(&prompt_abort);prompt.length=ui->job.length;
        memcpy(prompt.value,ui->job.secret,sizeof(prompt.value));
        fv_ui_wipe(ui->job.secret,sizeof(ui->job.secret));fv_ui_wipe(&ui->entry,sizeof(ui->entry));
        atomic_store(&prompt_state,3);
    }else if(state==4){fv_ui_fido_end(ui);atomic_store(&prompt_state,0);}
}
static bool ask(bool secret,uint16_t profile){
    memset(&prompt,0,sizeof(prompt));prompt.secret=secret;prompt.profile=profile;
    snprintf(prompt.label,sizeof(prompt.label),"%s",request_label);
    atomic_store(&prompt_abort,false);atomic_store(&prompt_state,1);uint32_t start=now(NULL);
    while(atomic_load(&prompt_state)!=3){
        if(fv_fido_cancelled(NULL) || (uint32_t)(now(NULL)-start)>=60000)atomic_store(&prompt_abort,true);
        fv_fido_pump();tight_loop_contents();
    }
    bool ok=atomic_load(&prompt_state)==3 && prompt.approved && !fv_fido_cancelled(NULL);
    /* Core 0 owns the mailbox until it acknowledges dismissal. Do not reuse it
     * or publish another modal before the old credentials have been wiped. */
    fv_fido_pump(); /* Drain the last disk job queued before core 0 answered. */
    atomic_store(&prompt_state,4);
    while(atomic_load(&prompt_state)!=0){fv_fido_pump();tight_loop_contents();}
    return ok;
}
static bool verify(void *ctx,const uint8_t *rp){
    (void)ctx;
    if(fv_fido_cancelled(NULL))return false;
    if(fv_fido_vault()->unlocked && fv_fido_verification_use_policy(&verification,now(NULL),rp,(fv_fido_uv_policy)uv_policy))return true;
    uint16_t profile=fv_fido_profile();
    bool ok=profile>=2 && profile<=4 && ask(true,profile);
    if(ok)ok=fv_fido_verify_secret(prompt.value,prompt.length)==0;
    fv_ui_wipe(prompt.value,sizeof(prompt.value));prompt.length=0;
    if(!ok || fv_fido_cancelled(NULL)){fv_fido_verification_clear(&verification);return false;}
    fv_fido_unlocked();return fv_fido_verification_use_policy(&verification,now(NULL),rp,(fv_fido_uv_policy)uv_policy);
}
static int presence(void *ctx){(void)ctx;return ask(false,0)?0:2;}
static bool commit(void *ctx,const uint8_t *data,size_t n){
    (void)ctx;return (!executing || !fv_fido_cancelled(NULL)) && n==sizeof(image) && fv_fido_store_commit(&store,data)==FV_FIDO_STORE_OK;
}
static uint8_t retries(void *ctx){
    (void)ctx;fv_device_state state;fv_vault *v=fv_fido_vault();
    if(!v->unlocked || v->platform->authority.load(v->platform->authority.context,&state) || state.status!=FV_ENROLLMENT_ACTIVE)return 0;
    uint32_t remaining=state.policy.max_attempts>state.attempts?state.policy.max_attempts-state.attempts:0;
    return remaining>255?255:(uint8_t)remaining;
}
static bool reset_allowed(void *ctx){(void)ctx;return atomic_load(&enumerated) && (uint32_t)(now(NULL)-atomic_load(&enumerated_at))<10000;}
static bool local(void *ctx){(void)ctx;return (!executing || !fv_fido_cancelled(NULL)) && fv_fido_vault()->unlocked;}
/* Display only bounded printable RP IDs. Refuse ambiguous/truncated labels.
 * This is host-supplied RP context, not independently verified browser origin. */
static bool field(CborValue *map,int key,CborValue *out){
    CborValue it;if(!cbor_value_is_map(map) || cbor_value_enter_container(map,&it))return false;
    while(!cbor_value_at_end(&it)){
        int k;if(!cbor_value_is_integer(&it) || cbor_value_get_int(&it,&k) || cbor_value_advance(&it))return false;
        if(k==key){*out=it;return true;}if(cbor_value_advance(&it))return false;
    }return false;
}
static bool label(const uint8_t *request,size_t size){
    snprintf(request_label,sizeof(request_label),"%s",request[0]==7?"RESET ALL PASSKEYS":request[0]==6?"VERIFY DEVICE FOR FIDO":"MANAGE PASSKEYS");
    if(request[0]!=1 && request[0]!=2)return true;
    CborParser parser;CborValue root,rp;
    if(size<2 || cbor_parser_init(request+1,size-1,0,&parser,&root) || !field(&root,request[0]==1?2:1,&rp))return false;
    if(request[0]==1){CborValue id;if(!cbor_value_is_map(&rp) || cbor_value_map_find_value(&rp,"id",&id))return false;rp=id;}
    char name[101];size_t n=sizeof(name);
    if(!cbor_value_is_text_string(&rp) || cbor_value_copy_text_string(&rp,name,&n,NULL) || !n || n>=sizeof(name))return false;
    for(size_t i=0;i<n;i++)if((unsigned char)name[i]<33 || (unsigned char)name[i]>126)return false;
    snprintf(request_label,sizeof(request_label),"%s %s",request[0]==1?"REGISTER":"SIGN IN",name);return true;
}
static bool ensure_engine(void){
    if(!engine_open){
        uint8_t key[32];
        if(fv_fido_store_open(&store,fv_fido_vault(),image)!=FV_FIDO_STORE_OK)return false;
        fv_fido_engine_ops_t ops={.random=fv_fido_random,.commit=commit,.presence=presence,.millis=now,
            .verify_user=verify,.uv_retries=retries,.cancelled=fv_fido_cancelled,.reset_allowed=reset_allowed,.local_authorized=local};
        bool ok=fv_fido_store_engine_key(&store,key)==FV_FIDO_STORE_OK &&
            fv_fido_engine_open(image,key,fv_fido_vault()->config.device_id,&ops);
        fv_ui_wipe(key,sizeof(key));if(!ok)return false;engine_open=true;
    }
    return fv_fido_engine_uv_policy(false,&uv_policy);
}
uint8_t fv_fido_policy_get(void){
    if(fv_fido_vault()->unlocked && !ensure_engine()){fv_fido_close();return 0;}
    return uv_policy;
}
int fv_fido_policy_set(uint8_t mode){
    if(mode>1 || !fv_fido_vault()->unlocked || !ensure_engine())return -1;
    if(!fv_fido_engine_uv_policy(true,&mode)){fv_fido_close();return -1;}
    uv_policy=mode;fv_fido_verification_clear(&verification);return 0;
}
int fv_fido_initialize(void){
    fv_fido_close();int r=fv_fido_store_initialize(&store,fv_fido_vault(),true,image);
    fv_fido_store_close(&store);fv_ui_wipe(image,sizeof(image));return r;
}
void fv_fido_execute(void){
    size_t n=1;response[0]=0x7f;invalidated=false;executing=true;
    if(fv_fido_cancelled(NULL))goto finish;
    uint16_t profile=fv_fido_profile();
    if(hid.rx_size==1 && hid.rx[0]==4){n=fv_fido_engine_info(profile>=2 && profile<=4,response,sizeof(response));goto finish;}
    if(!profile){response[0]=0x30;goto finish;}
    if(!hid.rx_size || !label(hid.rx,hid.rx_size)){response[0]=0x12;goto finish;}
    if(!fv_fido_vault()->unlocked && !verify(NULL,NULL)){response[0]=0x2d;goto finish;}
    if(!ensure_engine()){response[0]=0x30;goto finish;}
    n=fv_fido_engine_command_channel(hid.active_channel,hid.rx,hid.rx_size,response,sizeof(response));
 finish:
    executing=false;
    if(fv_fido_cancelled(NULL) || !fv_fido_vault()->unlocked || response[0]==0x7f)fv_fido_close();
    if(atomic_load(&cancel))fv_vault_lock(fv_fido_vault());
    completed_unlocked=fv_fido_vault()->unlocked;
    atomic_store(&done,(unsigned)(n?n:1));
}
void fv_fido_poll(void){
    if(!initialized){fv_fido_hid_reset(&hid);initialized=true;}
    if(atomic_exchange(&disconnected,false)){discard=true;if(!fv_fido_busy()){fv_fido_hid_reset(&hid);discard=false;}}
    unsigned n=atomic_exchange(&done,0);
    if(n){
        if(atomic_load(&cancel)){
            hid.cancelled=true;
            /* CANCEL can arrive after the worker published done. Serialize a
             * lock now so that this completion race cannot retain UV tokens. */
            atomic_store(&busy,false);
            (void)fv_usb_rpc((fv_usb_request){.op=FV_USB_LOCK});
            completed_unlocked=false;
        }
        if(!completed_unlocked)fv_usb_storage_clear_transport();
        fv_device_ui_media_changed(completed_unlocked);
        if(discard){fv_fido_hid_reset(&hid);discard=false;}
        else fv_fido_hid_complete(&hid,response,n);
        fv_ui_wipe(response,sizeof(response));atomic_store(&busy,false);
    }
    if(discard)return;
    if(hid.cancelled)atomic_store(&cancel,true);
    fv_fido_hid_tick(&hid,now(NULL));
    if(hid.pending && !fv_fido_busy() && !atomic_load(&fv_ui_maintenance) && fv_usb_worker_idle()){
        fv_command c={0};c.fido=true;
        atomic_store(&cancel,hid.cancelled);atomic_store(&busy,true);
        if(queue_try_add(&commands,&c)){hid.pending=false;keepalive_at=now(NULL);}
        else atomic_store(&busy,false);
    }
    if(!tud_hid_ready())return;
    uint8_t report[64];
    if(hid.control_pending){if(tud_hid_report(0,hid.control_report,64))hid.control_pending=false;}
    else if(fv_fido_hid_peek(&hid,report)){if(tud_hid_report(0,report,64))fv_fido_hid_sent(&hid);}
    else if(hid.processing && (uint32_t)(now(NULL)-keepalive_at)>=50){
        memset(report,0,sizeof(report));uint32_t c=hid.active_channel;
        report[0]=c>>24;report[1]=c>>16;report[2]=c>>8;report[3]=c;report[4]=0xbb;report[6]=1;
        report[7]=atomic_load(&prompt_state)?2:1;
        if(tud_hid_report(0,report,64))keepalive_at=now(NULL);
    }
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance){(void)instance;return fv_fido_hid_report_descriptor;}
uint16_t tud_hid_get_report_cb(uint8_t i,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t length){(void)i;(void)id;(void)type;(void)buffer;(void)length;return 0;}
void tud_hid_set_report_cb(uint8_t i,uint8_t id,hid_report_type_t type,const uint8_t *buffer,uint16_t length){
    (void)i;if(id || type!=HID_REPORT_TYPE_OUTPUT || length!=64 || discard)return;
    fv_fido_hid_receive(&hid,buffer,now(NULL));if(hid.cancelled)atomic_store(&cancel,true);
}
