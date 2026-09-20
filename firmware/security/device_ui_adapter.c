#include "bringup.h"
#include "device_ui_adapter.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
static fv_ui ui;
static fv_ui_job mailbox;
atomic_bool fv_ui_maintenance;
static atomic_bool disconnected;
static bool refresh;
static atomic_uint format_done,format_total;
static atomic_uint format_started_ms;
static uint64_t format_redraw;
void fv_device_ui_format_progress(void *context,uint64_t done,uint64_t total){
    (void)context;
    if(!done)atomic_store(&format_started_ms,(unsigned)(time_us_64()/1000));
    atomic_store(&format_done,(unsigned)done);
    atomic_store(&format_total,(unsigned)total);
}
void fv_device_ui_disconnect(void){atomic_store(&disconnected,true);}
void fv_device_ui_refresh(void){refresh=true;}
void fv_device_ui_media_changed(bool unlocked){if(ui.device.unlocked!=unlocked)refresh=true;}
void fv_device_ui_init(void){
    fv_ui_init(&ui);
    for(unsigned p=10;p<=15;p++){gpio_init(p);gpio_set_dir(p,GPIO_IN);gpio_disable_pulls(p);}
}
void fv_device_ui_poll(void){
    if(atomic_exchange(&disconnected,false)){fv_ui_cancel(&ui);refresh=true;}
    fv_ui_result r;
    if(queue_try_remove(&ui_responses,&r)){
        if(!r.unlocked)fv_usb_storage_clear_transport();
        fv_ui_complete(&ui,r);atomic_store(&fv_ui_maintenance,false);
    }
    if(refresh && ui.screen!=UI_WAIT){fv_ui_cancel(&ui);refresh=false;}
    static unsigned previous=0x3f,candidate=0x3f;static uint64_t changed;
    unsigned sample=(gpio_get_all()>>10)&0x3f;
    if(sample!=candidate){candidate=sample;changed=time_us_64();}
    if(sample!=previous && time_us_64()-changed>=25000){
        unsigned pressed=previous&~sample;previous=sample;
        const fv_ui_key keys[]={UI_UP,UI_LEFT,UI_DOWN,UI_RIGHT,UI_SELECT,UI_BACK};
        if(pressed && !(pressed&(pressed-1)))for(unsigned i=0;i<6;i++)if(pressed&(1u<<i))fv_ui_keypress(&ui,keys[i]);
    }
    if(ui.screen==UI_WAIT && ui.job.op==UI_CREATE){
        unsigned total=atomic_load(&format_total);
        uint64_t now=time_us_64();
        if(total && (!format_redraw || now-format_redraw>=250000)){
            ui.format_total=total;ui.format_done=atomic_load(&format_done);
            ui.format_milliseconds=(uint32_t)(now/1000)-atomic_load(&format_started_ms);
            fv_ui_render(&ui);format_redraw=now;
        }
    }
    if(ui.pending){
        atomic_store(&format_done,0);atomic_store(&format_total,0);
        format_redraw=0;
        fv_command c={0};mailbox=ui.job;c.ui=&mailbox;
        if(ui.job.op!=UI_STATUS)fv_usb_storage_clear_transport();
        atomic_store(&fv_ui_maintenance,true);
        if(queue_try_add(&commands,&c)){
            ui.pending=false;fv_ui_wipe(ui.job.secret,sizeof(ui.job.secret));ui.job.length=0;fv_ui_wipe(ui.job.current,64);ui.job.current_length=0;fv_ui_wipe(&ui.entry,sizeof(ui.entry));
        }else fv_ui_wipe(&mailbox,sizeof(mailbox));
        fv_ui_wipe(&c,sizeof(c));
    }
}
bool fv_device_ui_command(const char *command,char *out,size_t size){
#if FV_DEBUG_SCREEN
    if(!strcmp(command,"SCREEN")){
        int n=snprintf(out,size,"{\"command\":\"screen\",\"ok\":true,\"width\":160,\"height\":80,\"format\":\"mono-msb\",\"screen\":%u,\"busy\":%s,\"pixels\":\"",(unsigned)ui.screen,ui.screen==UI_WAIT?"true":"false");
        if(n<0 || (size_t)n+2*FV_SCREEN_BYTES+4>=size)return false;
        const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<FV_SCREEN_BYTES;i++){out[n++]=hex[ui.framebuffer[i]>>4];out[n++]=hex[ui.framebuffer[i]&15];}
        memcpy(out+n,"\"}\n",4);return true;
    }
    if(!strncmp(command,"KEY ",4)){
        const char *keys[]={"UP","DOWN","LEFT","RIGHT","SELECT","BACK"};unsigned i;
        for(i=0;i<6;i++)if(!strcmp(command+4,keys[i]))break;
        bool accepted=i<6 && ui.screen!=UI_WAIT;
        if(accepted)fv_ui_keypress(&ui,(fv_ui_key)(i+1));
        snprintf(out,size,"{\"command\":\"key\",\"ok\":true,\"accepted\":%s}\n",accepted?"true":"false");return true;
    }
#endif
    (void)command;(void)out;(void)size;return false;
}
