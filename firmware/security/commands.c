#include "bringup.h"
#if FV_USB_FIDO
#include "fido_adapter.h"
#endif
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
#endif
#include "fuse_vault/vault.h"
#include "fuse_vault/development_policy.h"
#include "fuse_vault/rp2354_authority.h"
#include "pico/flash.h"
#include "fuse_vault/random.h"
#include "fuse_vault/rp2354_entropy.h"
#include "fuse_vault/rp2354_sd.h"
#if FV_DEBUG_OTP_INSPECT
#include "fuse_vault/rp2354_otp_inspect.h"
#endif
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/version.h"
#include "hardware/clocks.h"
#include <mbedtls/platform_util.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
static size_t used;
static bool overflow;
static fv_random random_source;
static fv_rp2354_entropy entropy_source;
static fv_vault session;
static fv_enrollment_device persistent_device;
#if FV_DEBUG_ENROLLMENT
static fv_vault persistent_session;
#endif
static fv_vault_platform persistent_platform;
static int boot_recovery;
static fv_rp2354_sd_t sd;
static bool sd_initialized;
static uint8_t buffer[32768] __attribute__((aligned(4)));
/* TEST enrollment only, never persisted to flash/OTP and never exported. */
static struct {fv_device_state state;uint8_t root[32],token[32];bool destroyed;} enrollment;
static const uint8_t credential[]="public bring-up credential",replacement[]="public replacement credential";
static void append(const char *format,...) __attribute__((format(printf,1,2)));
static void append(const char *format,...) {
    if(overflow)return;
    va_list args;va_start(args,format);
    int n=vsnprintf(reply+used,FV_REPLY_BYTES-used,format,args);va_end(args);
    if(n<0 || (size_t)n>=FV_REPLY_BYTES-used){overflow=true;return;}
    used+=(size_t)n;
}
static bool start_random(void) {
    fv_rp2354_entropy_init(&entropy_source);
    return mbedtls_ctr_drbg_self_test(0)==0 &&
        fv_random_init(&random_source,fv_rp2354_entropy_block,&entropy_source)==0;
}
static void entropy_status(void) {
    append("\"entropy_blocks\":%"PRIu32",\"entropy_us\":%""llu"",\"trng_status\":%"PRIu32
        ",\"autocorrelation\":%"PRIu32",\"entropy_failures\":%"PRIu32",\"timeouts\":%"PRIu32,
        entropy_source.blocks,(unsigned long long)entropy_source.elapsed_us,entropy_source.last_status,
        entropy_source.autocorrelation,entropy_source.failures,entropy_source.timeouts);
}
static void rng_test(void) {
    bool ok=start_random();
    uint8_t a[32]={0},b[32]={0};
    if(ok)ok=!fv_random_generate(&random_source,a,32) && !fv_random_generate(&random_source,b,32) && memcmp(a,b,32);
    append("{\"command\":\"rng\",\"ok\":%s,",ok?"true":"false");entropy_status();append("}\n");
    mbedtls_platform_zeroize(a,32);mbedtls_platform_zeroize(b,32);fv_random_clear(&random_source);
}
static void kdf_test(uint32_t iterations) {
    fv_envelope_config config={.volume={.volume_id={1},.logical_blocks=64,.layer_count=2,.cipher_ids={1,2}},
        .device_id={2},.credential_generation=1,.credential_profile=1,.iterations=iterations,.policy={10,FV_LIMIT_DESTROY}};
    alignas(4) uint8_t header[512];uint8_t binding[32]={0},vmk[32]={0},recovered[32]={0};
    uint64_t seal_us=0,open_us=0,reject_us=0;bool ok=fv_hmac_self_test() && start_random();
    fv_kdf_limits limits={1,1000000};
    if(ok)ok=!fv_random_generate(&random_source,binding,32) && !fv_random_generate(&random_source,vmk,32);
    if(ok) {
        uint64_t start=time_us_64();
        ok=!fv_envelope_seal(&config,2200,limits,binding,credential,sizeof(credential)-1,vmk,
                             fv_random_generate,&random_source,header);seal_us=time_us_64()-start;
        start=time_us_64();
        if(ok)ok=!fv_envelope_open(header,2200,limits,binding,credential,sizeof(credential)-1,recovered) && fv_tag_equal(vmk,recovered);
        open_us=time_us_64()-start;start=time_us_64();
        if(ok)ok=fv_envelope_open(header,2200,limits,binding,(const uint8_t*)"wrong",5,recovered)!=0;
        reject_us=time_us_64()-start;
    }
    append("{\"command\":\"kdf\",\"ok\":%s,\"iterations\":%"PRIu32",\"seal_us\":%""llu"
        ",\"unlock_us\":%""llu"",\"reject_us\":%""llu"",",ok?"true":"false",iterations,(unsigned long long)seal_us,(unsigned long long)open_us,(unsigned long long)reject_us);
    entropy_status();append("}\n");
    mbedtls_platform_zeroize(binding,32);mbedtls_platform_zeroize(vmk,32);mbedtls_platform_zeroize(recovered,32);
    fv_random_clear(&random_source);
}
static int load_state(void *unused,fv_device_state *s){(void)unused;*s=enrollment.state;return 0;}
static int commit_state(void *unused,uint64_t previous,const fv_device_state *s) {
    (void)unused;if(enrollment.state.sequence!=previous || s->sequence!=previous+1)return -1;
    enrollment.state=*s;return 0;
}
static int binding(void *unused,const uint8_t id[16],uint32_t slot,uint8_t out[32]) {
    (void)unused;if(enrollment.destroyed || slot)return -1;
    return fv_vault_binding(enrollment.root,enrollment.token,slot,id,out);
}
static int destroy(void *unused,uint32_t slot) {
    (void)unused;if(slot)return -1;
    enrollment.destroyed=true;mbedtls_platform_zeroize(enrollment.token,32);return 0;
}
static void pattern(uint64_t lba) {
    for(size_t i=0;i<sizeof(buffer);i++)buffer[i]=(uint8_t)((lba*512+i)*29+17);
}
static bool pattern_matches(uint64_t lba) {
    for(size_t i=0;i<sizeof(buffer);i++)if(buffer[i]!=(uint8_t)((lba*512+i)*29+17))return false;
    return true;
}
static void vault_test(void) {
    const char *phase="self-test";int result=-1;uint64_t create_us=0,unlock_us=0,write_us=0,read_us=0,rewrap_us=0;
    memset(&enrollment,0,sizeof(enrollment));
    fv_vault_platform platform={.sd=&sd.interface,.authority={NULL,load_state,commit_state,binding,destroy},
        .random=fv_random_generate,.random_context=&random_source,.kdf_limits={1,1000000}};
    if(!fv_hmac_self_test() || !start_random())goto done;
    phase="sd-init";
    bool ready=sd_initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);sd_initialized=true;
    if(!ready)goto done;
    phase="test-enrollment";
    enrollment.state.status=FV_ENROLLMENT_EMPTY;
    if(fv_random_generate(&random_source,enrollment.root,32) || fv_random_generate(&random_source,enrollment.token,32) ||
       fv_random_generate(&random_source,enrollment.state.device_id,16))goto done;
    phase="create";uint64_t start=time_us_64();
    const uint16_t algorithms[4]={1,2,0,0};
    result=fv_vault_create(&platform,2048,algorithms,2,1,FV_ENROLLMENT_ITERATIONS,fv_auth_policy_default(),credential,sizeof(credential)-1);
    create_us=time_us_64()-start;if(result)goto done;
    phase="unlock";start=time_us_64();
    result=fv_vault_unlock(&session,&platform,credential,sizeof(credential)-1);unlock_us=time_us_64()-start;if(result)goto done;
    phase="write";start=time_us_64();
    for(uint64_t lba=0;lba<2048;lba+=64) {
        pattern(lba);result=fv_vault_write(&session,lba,64,buffer);if(result)goto done;
    }
    write_us=time_us_64()-start;fv_vault_lock(&session);
    phase="reopen";result=fv_vault_unlock(&session,&platform,credential,sizeof(credential)-1);if(result)goto done;
    phase="read";start=time_us_64();
    for(uint64_t lba=0;lba<2048;lba+=64) {
        result=fv_vault_read(&session,lba,64,buffer);if(result)goto done;
        if(!pattern_matches(lba)){result=-100;goto done;}
    }
    read_us=time_us_64()-start;
    phase="rewrap";start=time_us_64();
    result=fv_vault_change_credential(&session,&platform,credential,sizeof(credential)-1,replacement,sizeof(replacement)-1,
                                     1,FV_ENROLLMENT_ITERATIONS,fv_auth_policy_default(),false);
    rewrap_us=time_us_64()-start;if(result)goto done;
    phase="old-credential";
    if(fv_vault_unlock(&session,&platform,credential,sizeof(credential)-1)!=FV_VAULT_AUTH){result=-101;goto done;}
    phase="new-credential";result=fv_vault_unlock(&session,&platform,replacement,sizeof(replacement)-1);if(result)goto done;
    phase="rewrapped-read";
    for(uint64_t lba=0;lba<2048;lba+=64) {
        result=fv_vault_read(&session,lba,64,buffer);if(result)goto done;
        if(!pattern_matches(lba)){result=-102;goto done;}
    }
    phase="complete";
done:
    append("{\"command\":\"vault\",\"ok\":%s,\"phase\":\"%s\",\"result\":%d,\"authority\":\"volatile-test-only\","
        "\"bytes\":1048576,\"create_us\":%""llu"",\"unlock_us\":%""llu"",\"write_us\":%""llu"
        ",\"read_us\":%""llu"",\"rewrap_us\":%""llu"",",!strcmp(phase,"complete")?"true":"false",phase,result,
        (unsigned long long)create_us,(unsigned long long)unlock_us,(unsigned long long)write_us,(unsigned long long)read_us,(unsigned long long)rewrap_us);
    entropy_status();append("}\n");
    fv_vault_lock(&session);fv_random_clear(&random_source);
    mbedtls_platform_zeroize(&enrollment,sizeof(enrollment));mbedtls_platform_zeroize(buffer,sizeof(buffer));
}
#if FV_DEBUG_OTP_INSPECT
static void otp_snapshot(void) {
    char id[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];pico_get_unique_board_id_string(id,sizeof(id));
    append("{\"command\":\"otp\",\"schema\":1,\"ok\":true,\"device_id\":\"%s\",\"pages\":[",id);
    for(unsigned page=0;page<64;page++) {
        fv_otp_page_summary s;fv_rp2354_otp_inspect(page,&s);
        append("%s{\"page\":%u,\"lock_raw\":[%"PRIu32",%"PRIu32"],\"lock_status\":[%"PRId32",%"PRId32"],"
            "\"software_lock\":%"PRIu32",\"blank\":%u,\"programmed\":%u,\"all_ones\":%u,\"unreadable\":%u,\"read_error\":%"PRId32"}",
            page?",":"",page,s.lock_raw[0],s.lock_raw[1],s.lock_status[0],s.lock_status[1],s.software_lock,
            s.blank,s.programmed,s.all_ones,s.unreadable,s.first_read_error);
    }
    append("]}\n");
}
#endif
static void persistent_status(void) {
    fv_enrollment_inventory inventory=fv_enrollment_inspect(&persistent_device);
    int opened=fv_enrollment_open(&persistent_device);fv_device_state s={0};
    int loaded=opened==0?fv_journal_load(&persistent_device.journal,&s):-1;
    append("{\"command\":\"state\",\"ok\":true,\"open_result\":%d,\"boot_recovery\":%d,"
        "\"journal_result\":%d,\"status\":%u,\"sequence\":%llu,\"credential_generation\":%llu,"
        "\"attempts\":%u,\"pending\":%s,\"token_slot\":%u,\"root_page\":%u,\"token_page\":%u,"
        "\"flash_offset\":%u,\"iterations\":%u,\"root_blank\":%d,\"tokens_blank\":%d,\"flash_blank\":%d}\n",opened,boot_recovery,loaded,(unsigned)s.status,
        (unsigned long long)s.sequence,(unsigned long long)s.credential_generation,(unsigned)s.attempts,
        s.attempt_pending?"true":"false",(unsigned)s.token_slot,FV_OTP_ROOT_PAGE,(unsigned)(FV_OTP_TOKEN_PAGE+s.token_slot),
        FV_SECURITY_FLASH_OFFSET,FV_ENROLLMENT_ITERATIONS,inventory.root_blank,inventory.tokens_blank,inventory.flash_blank);
}
#if FV_DEBUG_ENROLLMENT
static bool confirm_device(const char *command,const char *prefix) {
    char id[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];pico_get_unique_board_id_string(id,sizeof(id));
    size_t n=strlen(prefix);return !strncmp(command,prefix,n) && !strcmp(command+n,id);
}
static void provision(void) {
    fv_vault_lock(&persistent_session);
    int r=-1;
    if(fv_hmac_self_test() && start_random())r=fv_enrollment_provision(&persistent_device,fv_random_generate,&random_source);
    fv_random_clear(&random_source);
    append("{\"command\":\"provision\",\"ok\":%s,\"result\":%d}\n",r?"false":"true",r);
}
static void prepare_flash(void) {
    fv_vault_lock(&persistent_session);
    int r=fv_enrollment_prepare_flash(&persistent_device);
    append("{\"command\":\"prepare-flash\",\"ok\":%s,\"result\":%d}\n",r?"false":"true",r);
}
static void destroy_enrollment(void) {
    fv_vault_lock(&persistent_session);
    int r=fv_enrollment_open(&persistent_device);
    if(!r)r=fv_enrollment_request_destruction(&persistent_device);
    append("{\"command\":\"destroy\",\"ok\":%s,\"result\":%d}\n",r?"false":"true",r);
}
/* Every command ends locked. Secrets stay on device; public test credentials
 * are only available in this compile-time debug interface. */
static void persistent_test(unsigned op,bool use_replacement) {
    fv_vault_lock(&persistent_session);const char *phase="authority";int r=fv_enrollment_open(&persistent_device);
    uint64_t start=time_us_64();
    const uint8_t *secret=use_replacement?replacement:credential;
    size_t bytes=use_replacement?sizeof(replacement)-1:sizeof(credential)-1;
    if(r)goto done;
    phase="recover";r=fv_vault_recover(&persistent_platform);if(r)goto done;
    phase="sd-init";
    bool ready=sd_initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);sd_initialized=true;
    if(!ready){r=-1;goto done;}
    if(op==0) {
        phase="rng";if(!start_random()){r=-1;goto done;}
        const uint16_t algorithms[4]={1,2,0,0};phase="create";
        r=fv_vault_create(&persistent_platform,2048,algorithms,2,1,FV_ENROLLMENT_ITERATIONS,
            fv_auth_policy_default(),credential,sizeof(credential)-1);if(r)goto done;
    }
    phase="unlock";
    if(op==3){secret=(const uint8_t*)"wrong test credential";bytes=21;}
    r=fv_vault_unlock(&persistent_session,&persistent_platform,secret,bytes);
    if(op==3) {
        /* Expected rejection is reported as a successful diagnostic, with actual result. */
        phase=r==FV_VAULT_AUTH || r==FV_VAULT_DENIED?"expected-rejection":"unexpected";goto done;
    }
    if(r)goto done;
    if(op==2) {
        phase="rng";if(!start_random()){r=-1;goto done;}
        phase="change";
        const uint8_t *next=use_replacement?credential:replacement;
        size_t next_bytes=use_replacement?sizeof(credential)-1:sizeof(replacement)-1;
        r=fv_vault_change_credential(&persistent_session,&persistent_platform,secret,bytes,next,next_bytes,
            1,FV_ENROLLMENT_ITERATIONS,fv_auth_policy_default(),false);if(r)goto done;
    } else {
        phase=op==0?"write":"verify";
        for(uint64_t lba=0;lba<2048;lba+=64) {
            if(op==0){pattern(lba);r=fv_vault_write(&persistent_session,lba,64,buffer);}
            else {r=fv_vault_read(&persistent_session,lba,64,buffer);if(!r && !pattern_matches(lba))r=-100;}
            if(r)goto done;
        }
    }
    phase="complete";
done:
    fv_vault_lock(&persistent_session);fv_random_clear(&random_source);mbedtls_platform_zeroize(buffer,sizeof(buffer));
    append("{\"command\":\"persistent\",\"ok\":%s,\"operation\":%u,\"phase\":\"%s\",\"result\":%d,"
        "\"elapsed_us\":%llu,\"authority\":\"OTP-and-flash\",\"iterations\":%u}\n",
        (!strcmp(phase,"complete") || !strcmp(phase,"expected-rejection"))?"true":"false",op,phase,r,
        (unsigned long long)(time_us_64()-start),FV_ENROLLMENT_ITERATIONS);
}
#endif
#if FV_USB_MSC
static fv_vault usb_session;
static uint32_t usb_generation;
#if FV_USB_FIDO
static bool fido_rng_ready;
#endif
static void usb_lock(void) {
#if FV_USB_FIDO
    fv_fido_close();
#endif
    fv_vault_lock(&usb_session);++usb_generation;
}
void fv_usb_storage_execute(const fv_usb_request *request,fv_usb_response *response) {
    int result=FV_BLOCK_OK;
    switch(request->op) {
    case FV_USB_LOCK:usb_lock();break;
    case FV_USB_READ:
    case FV_USB_WRITE:
        if(request->count>FV_USB_IO_BYTES/512 || !request->count)result=FV_BLOCK_ERROR_INVALID_ARGUMENT;
        else if(request->op==FV_USB_READ)result=fv_vault_read(&usb_session,request->lba,request->count,request->data);
        else result=fv_vault_write(&usb_session,request->lba,request->count,request->data);
        /* Stop serving the medium on authentication or physical I/O failure. */
        if(result!=FV_BLOCK_OK && result!=FV_BLOCK_ERROR_OUT_OF_RANGE &&
           result!=FV_BLOCK_ERROR_INVALID_ARGUMENT)usb_lock();
        break;
    case FV_USB_SYNC:
        /* No buffered writes: each fv_vault_write has already synced. */
        if(!usb_session.unlocked)result=FV_BLOCK_ERROR_NOT_READY;
        break;
    case FV_USB_STATUS:break;
    default:result=FV_BLOCK_ERROR_INVALID_ARGUMENT;break;
    }
    *response=(fv_usb_response){.result=result,.generation=usb_generation,
        .unlocked=usb_session.unlocked,
        .blocks=usb_session.unlocked?(uint32_t)usb_session.config.volume.logical_blocks:0};
}
#if FV_DEVICE_UI
void fv_device_ui_execute(const fv_ui_job *job,fv_ui_result *out) {
    int r=0,opened;fv_device_state state={0};
    if(job->op!=UI_STATUS && job->op!=UI_ERASE && job->op!=UI_LOCK && job->op!=UI_FIDO_INIT && job->op!=UI_FIDO_POLICY) {
        if(!job->length || job->length>64)r=FV_VAULT_INVALID;
        if(job->profile<2 || job->profile>4)r=FV_VAULT_INVALID;
        if(job->profile>=3 && job->length!=4)r=FV_VAULT_INVALID;
        for(unsigned i=0;i<job->length && !r;i++){
            if(job->profile==2?(job->secret[i]<1 || job->secret[i]>4):job->secret[i]>=(job->profile==3?100:64))r=FV_VAULT_INVALID;
        }
    }
    if((job->op==UI_CREATE || job->op==UI_POLICY) &&
       (!job->attempts || job->attempts>100 || (job->action!=1 && job->action!=2)))r=FV_VAULT_INVALID;
    if(job->op==UI_CHANGE && (!job->current_length || job->current_length>64 || (job->profile==2 && job->length<8)))r=FV_VAULT_INVALID;
    if(job->op==UI_CREATE){
        if((job->profile==2 && job->length<8) || !job->count || job->count>4)r=FV_VAULT_INVALID;
        for(unsigned i=0;i<4;i++)if(i<job->count?(job->algorithms[i]!=1 && job->algorithms[i]!=2):job->algorithms[i]!=0)r=FV_VAULT_INVALID;
    }
    if(r)goto done;
    if(job->op==UI_LOCK){usb_lock();goto done;}
    if(job->op==UI_ERASE){
        if(!usb_session.unlocked){r=FV_VAULT_DENIED;goto done;}
        usb_lock();r=fv_enrollment_request_destruction(&persistent_device);goto done;
    }
    if(job->op==UI_STATUS)goto done;
#if FV_USB_FIDO
    if(job->op==UI_FIDO_INIT){r=fv_fido_initialize();goto done;}
    if(job->op==UI_FIDO_POLICY){r=fv_fido_policy_set(job->fido_policy);goto done;}
#endif
    if(job->op!=UI_CREATE && job->op!=UI_UNLOCK && job->op!=UI_POLICY && job->op!=UI_CHANGE){r=FV_VAULT_INVALID;goto done;}
    usb_lock();
    bool ready=sd_initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);sd_initialized=true;
    if(!ready){r=FV_VAULT_IO;goto done;}
    if(job->op==UI_UNLOCK){
        r=fv_vault_unlock(&usb_session,&persistent_platform,job->secret,job->length);
        if(!r && usb_session.config.volume.logical_blocks>UINT32_MAX){usb_lock();r=FV_VAULT_INVALID;}
        ++usb_generation;
#if FV_USB_FIDO
        if(!r)fv_fido_unlocked();
#endif
        goto done;
    }
    if(!start_random()){r=FV_VAULT_IO;goto done;}
    fv_auth_policy policy={job->attempts,(fv_limit_action)job->action};
    if(job->op==UI_CHANGE){
        r=persistent_platform.authority.load(persistent_platform.authority.context,&state);
        if(!r)r=fv_vault_change_credential(&usb_session,&persistent_platform,job->current,job->current_length,
            job->secret,job->length,job->profile,FV_ENROLLMENT_ITERATIONS,state.policy,false);
        goto done;
    }
    if(job->op==UI_POLICY){
        r=fv_vault_change_credential(&usb_session,&persistent_platform,job->secret,job->length,
            job->secret,job->length,job->profile,FV_ENROLLMENT_ITERATIONS,policy,true);
        goto done;
    }
    /* Reuse the existing 32 KiB diagnostic workspace; no extra RAM allocation. */
    persistent_platform.format_scratch=buffer;
    persistent_platform.format_sectors=sizeof(buffer)/512;
    persistent_platform.format_progress=fv_device_ui_format_progress;
    uint64_t blocks=fv_volume_max_blocks(sd.interface.ops->block_count(&sd.interface));
    if(!blocks){r=FV_VAULT_INVALID;goto done;}
    opened=fv_enrollment_open(&persistent_device);
    if(opened==1 || opened==2)r=fv_enrollment_provision(&persistent_device,fv_random_generate,&random_source);
    else if(opened)r=opened;
    else {
        r=persistent_platform.authority.load(persistent_platform.authority.context,&state);
        if(!r && state.status==FV_ENROLLMENT_DESTROYED)r=fv_enrollment_provision(&persistent_device,fv_random_generate,&random_source);
        else if(!r && state.status!=FV_ENROLLMENT_EMPTY)r=FV_VAULT_STATE;
    }
    if(!r)r=fv_vault_create(&persistent_platform,blocks,job->algorithms,job->count,job->profile,
        FV_ENROLLMENT_ITERATIONS,policy,job->secret,job->length);
 done:
    memset(out,0,sizeof(*out));out->result=r;
    opened=fv_enrollment_open(&persistent_device);
    if(!opened){
        int loaded=persistent_platform.authority.load(persistent_platform.authority.context,&state);
        if(!loaded){out->status=state.status;out->attempts=state.policy.max_attempts;out->action=state.policy.limit_action;}
        else if(!out->result)out->result=loaded;
    }else if(opened!=1 && opened!=2 && !out->result)out->result=opened;
    if(!out->result && out->status==FV_ENROLLMENT_ACTIVE){
        if(usb_session.unlocked)out->profile=usb_session.config.credential_profile;
        else {
            if(!sd_initialized || !sd.interface.ops->is_present(&sd.interface)){
                bool ready=sd_initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);sd_initialized=true;
                if(!ready)out->result=FV_VAULT_IO;
            }
            if(!out->result)out->result=fv_vault_credential_profile(&persistent_platform,&out->profile);
        }
    }
#if FV_USB_FIDO
    if(usb_session.unlocked)out->fido_policy=fv_fido_policy_get();
    fido_rng_ready=false;
#endif
    fv_random_clear(&random_source);
    out->unlocked=usb_session.unlocked;
    out->blocks=usb_session.unlocked?usb_session.config.volume.logical_blocks:0;
}
#endif
static bool usb_session_command(const char *command) {
    if(!strcmp(command,"INFO") || !strcmp(command,"STATE"))return false;
    int r=0;
    if(!strcmp(command,"LOCK"))usb_lock();
    else if(FV_DEBUG_SESSION && (!strcmp(command,"UNLOCK ORIGINAL") || !strcmp(command,"UNLOCK REPLACEMENT"))) {
        /* Repeated unlock is idempotent while already open; no extra guess charged. */
        if(!usb_session.unlocked) {
            bool ready=sd_initialized?fv_rp2354_sd_reinitialize(&sd):fv_rp2354_sd_init(&sd);
            sd_initialized=true;
            if(!ready)r=FV_VAULT_IO;
            else {
                bool changed=!strcmp(command,"UNLOCK REPLACEMENT");
                r=fv_vault_unlock(&usb_session,&persistent_platform,changed?replacement:credential,
                    changed?sizeof(replacement)-1:sizeof(credential)-1);
                if(!r && usb_session.config.volume.logical_blocks>UINT32_MAX) {
                    usb_lock();r=FV_VAULT_INVALID;
                }
                if(!r)++usb_generation;
            }
        }
    } else if(strcmp(command,"MEDIA")) {
        append("{\"command\":\"error\",\"ok\":false,\"error\":\"command unavailable in USB storage build\"}\n");
        return true;
    }
    append("{\"command\":\"media\",\"ok\":%s,\"result\":%d,\"unlocked\":%s,"
        "\"blocks\":%"PRIu32",\"block_bytes\":512,\"generation\":%"PRIu32"}\n",
        r?"false":"true",r,usb_session.unlocked?"true":"false",
        usb_session.unlocked?(uint32_t)usb_session.config.volume.logical_blocks:0,usb_generation);
    return true;
}
#endif
void security_execute(const char *command) {
    used=0;overflow=false;
#if FV_USB_MSC
    if(usb_session_command(command))return;
#endif
    if(!strcmp(command,"INFO")) {
        char id[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];pico_get_unique_board_id_string(id,sizeof(id));
        append("{\"command\":\"info\",\"ok\":true,\"protocol\":1,\"device_id\":\"%s\",\"sdk\":\"%s\","
            "\"chip_revision\":%u,\"rom_revision\":%u,\"cpu_hz\":%"PRIu32",\"sd_hz\":%u,\"otp_inspection\":%s,"
            "\"usb_msc\":%s,\"device_ui\":%s,\"debug_screen\":%s,\"debug_session\":%s,\"otp_writes\":true,\"persistent_authority\":true,\"debug_enrollment\":%s,\"hmac_backend\":%u,\"session_bytes\":%u,\"usb_fido\":%s,\"worker_stack_bytes\":%u,\"production_ready\":false}\n",
            id,PICO_SDK_VERSION_STRING,rp2350_chip_version(),rp2350_rom_version(),clock_get_hz(clk_sys),FV_SD_CLOCK_HZ,
            FV_DEBUG_OTP_INSPECT?"true":"false",FV_USB_MSC?"true":"false",FV_DEVICE_UI?"true":"false",FV_DEBUG_SCREEN?"true":"false",FV_DEBUG_SESSION?"true":"false",FV_DEBUG_ENROLLMENT?"true":"false",fv_hmac_backend(),(unsigned)sizeof(session),FV_USB_FIDO?"true":"false",FV_USB_FIDO?65536u:32768u);
    } else if(!strcmp(command,"RNG"))rng_test();
    else if(!strcmp(command,"VAULT ERASE_SD")) {
        if(fv_enrollment_open(&persistent_device)!=1)
            append("{\"command\":\"vault\",\"ok\":false,\"error\":\"OTP allocation is not virgin; use persistent commands\"}\n");
        else vault_test();
    }
    else if(!strcmp(command,"STATE"))persistent_status();
#if FV_DEBUG_ENROLLMENT
    else if(confirm_device(command,"PROVISION "))provision();
    else if(confirm_device(command,"PREPARE_FLASH "))prepare_flash();
    else if(confirm_device(command,"DESTROY "))destroy_enrollment();
    else if(confirm_device(command,"CREATE_ERASE_SD "))persistent_test(0,false);
    else if(!strcmp(command,"CHECK ORIGINAL"))persistent_test(1,false);
    else if(!strcmp(command,"CHECK REPLACEMENT"))persistent_test(1,true);
    else if(!strcmp(command,"CHANGE ORIGINAL"))persistent_test(2,false);
    else if(!strcmp(command,"CHANGE REPLACEMENT"))persistent_test(2,true);
    else if(!strcmp(command,"WRONG"))persistent_test(3,false);
#endif
#if FV_DEBUG_OTP_INSPECT
    else if(!strcmp(command,"OTP"))otp_snapshot();
#endif
    else {
        unsigned iterations=0;int consumed=0;
        if(sscanf(command,"KDF %u%n",&iterations,&consumed)==1 && consumed && !command[consumed] && iterations>=1 && iterations<=1000000)
            kdf_test(iterations);
        else append("{\"command\":\"error\",\"ok\":false,\"error\":\"unknown or invalid command\"}\n");
    }
    if(overflow)snprintf(reply,FV_REPLY_BYTES,"{\"command\":\"error\",\"ok\":false,\"error\":\"reply overflow\"}\n");
}
void security_worker(void) {
#if FV_DEBUG_STARTUP
    startup_stage=10;
#endif
    if(!flash_safe_execute_core_init())panic("flash lockout init");
#if FV_DEBUG_STARTUP
    startup_stage=11;
#endif
    fv_rp2354_authority_init(&persistent_device);
    persistent_platform=(fv_vault_platform){.sd=&sd.interface,.authority=fv_enrollment_authority(&persistent_device),
        .random=fv_random_generate,.random_context=&random_source,.kdf_limits=FV_ENROLLMENT_KDF_LIMITS};
#if FV_DEBUG_STARTUP
    startup_stage=12;
#endif
    boot_recovery=fv_enrollment_open(&persistent_device);
#if FV_DEBUG_STARTUP
    startup_stage=13;
#endif
    if(!boot_recovery)boot_recovery=fv_vault_recover(&persistent_platform);
#if FV_DEBUG_STARTUP
    startup_stage=14;
#endif
    for(;;) {
        fv_command command;queue_remove_blocking(&commands,&command);
#if FV_USB_FIDO
        if(command.fido){fido_rng_ready=start_random();fv_fido_execute();fv_random_clear(&random_source);fido_rng_ready=false;continue;}
#endif
#if FV_DEVICE_UI
        if(command.ui){
            fv_ui_job job=*command.ui;fv_ui_wipe(command.ui,sizeof(job));
            fv_ui_result result;fv_device_ui_execute(&job,&result);fv_ui_wipe(&job,sizeof(job));
            fv_ui_wipe(&command,sizeof(command));queue_add_blocking(&ui_responses,&result);continue;
        }
#endif
#if FV_USB_MSC
        if(command.storage.op!=FV_USB_NONE) {
            fv_usb_response response;
            fv_usb_storage_execute(&command.storage,&response);
            queue_add_blocking(&storage_responses,&response);
            continue;
        }
#endif
        security_execute(command.text);
        uint32_t n=(uint32_t)strlen(reply);queue_add_blocking(&responses,&n);
    }
}

#if FV_USB_FIDO
fv_vault *fv_fido_vault(void){return &usb_session;}
bool fv_fido_random(void *ctx,uint8_t *out,size_t n){(void)ctx;if(!fido_rng_ready)fido_rng_ready=start_random();return fido_rng_ready && fv_random_generate(&random_source,out,n)==0;}
uint16_t fv_fido_profile(void){
    uint16_t profile=0;
    if(sd_initialized && !sd.interface.ops->is_present(&sd.interface)){usb_lock();return 0;}
    if(usb_session.unlocked)return usb_session.config.credential_profile;
    bool ready=sd_initialized?sd.interface.ops->is_present(&sd.interface):fv_rp2354_sd_init(&sd);
    sd_initialized=true;
    if(ready)(void)fv_vault_credential_profile(&persistent_platform,&profile);
    return profile;
}
int fv_fido_verify_secret(const uint8_t *secret,size_t n){
    int r=usb_session.unlocked?fv_vault_reverify(&usb_session,secret,n):fv_vault_unlock(&usb_session,&persistent_platform,secret,n);
    if(r || usb_session.config.volume.logical_blocks>UINT32_MAX){usb_lock();return r?r:FV_VAULT_INVALID;}
    ++usb_generation;return 0;
}
void fv_fido_pump(void){
    fv_command c;if(!queue_try_remove(&commands,&c))return;
    if(c.storage.op!=FV_USB_NONE){fv_usb_response r;fv_usb_storage_execute(&c.storage,&r);queue_add_blocking(&storage_responses,&r);}
    else if(c.ui){fv_ui_result r={.result=FV_VAULT_DENIED};fv_ui_wipe(c.ui,sizeof(*c.ui));queue_add_blocking(&ui_responses,&r);}
    else {snprintf(reply,FV_REPLY_BYTES,"{\"ok\":false,\"error\":\"FIDO busy\"}\n");uint32_t n=strlen(reply);queue_add_blocking(&responses,&n);}
}
#endif
