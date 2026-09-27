/* Offline visual fixtures, rendered by the actual device UI. No vault or USB. */
#include "device_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(fv_ui *u,const char *name){
    char path[96];snprintf(path,sizeof(path),"%s.pbm",name);
    fv_ui_render(u);FILE *f=fopen(path,"wb");
    if(!f){perror(path);exit(1);}
    fputs("P4\n160 80\n",f);
    if(fwrite(u->framebuffer,1,FV_SCREEN_BYTES,f)!=FV_SCREEN_BYTES || fclose(f))exit(1);
}
int main(void){
    fv_ui u;fv_ui_init(&u);u.fido_enabled=true;
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.attempts=10,.action=1});
    save(&u,"01-locked");
    fv_ui_keypress(&u,UI_SELECT);
    for(unsigned i=0;i<8;i++)fv_ui_keypress(&u,(fv_ui_key)(i%4+1));
    save(&u,"02-pattern");
    fv_ui_fido_begin(&u,true,3,"VERIFY");save(&u,"03-code-wheels");
    fv_ui_fido_begin(&u,true,4,"VERIFY");save(&u,"04-word-entry");
    fv_ui_fido_end(&u);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true,.blocks=115000000,.attempts=10,.action=1});
    u.read_kib_tenths=12340;u.write_kib_tenths=25600;save(&u,"05-open");
    fv_ui_keypress(&u,UI_DOWN);fv_ui_keypress(&u,UI_SELECT);save(&u,"06-settings");
    fv_ui_keypress(&u,UI_UP);save(&u,"07-settings-bottom");
    fv_ui_keypress(&u,UI_UP);fv_ui_keypress(&u,UI_SELECT);save(&u,"08-verification-timed");
    fv_ui_keypress(&u,UI_DOWN);save(&u,"09-verification-session");
    fv_ui_fido_begin(&u,false,0,"SIGN IN github.com");save(&u,"10-approval");
    fv_ui_fido_end(&u);u.job.op=UI_PASSKEY_LIST;
    fv_ui_result r={.status=2,.profile=2,.unlocked=true,.passkey_count=2,.passkey_id={1}};
    strcpy(r.passkey_site,"github.com");strcpy(r.passkey_account,"alice@example.com");
    fv_ui_complete(&u,r);save(&u,"11-passkey");
    fv_ui_keypress(&u,UI_SELECT);save(&u,"12-delete");
    strcpy(r.passkey_site,"login.a-long-subdomain.for-the-preview.example.com");
    strcpy(r.passkey_account,"alice.with-a-long-account-name+test-account@example.com");
    fv_ui_complete(&u,r);fv_ui_keypress(&u,UI_RIGHT);save(&u,"13-long-label-tail");
    fv_ui_keypress(&u,UI_SELECT);save(&u,"14-delete-tail");
    r.passkey_count=0;fv_ui_complete(&u,r);save(&u,"15-empty-passkeys");
    u.screen=UI_ERASE_CONFIRM;u.cursor=0;save(&u,"16-destroy");
    u.screen=UI_FIDO_INIT_CONFIRM;save(&u,"17-initialize-fido");
    u.screen=UI_REVIEW;u.flow=UI_FLOW_POLICY;u.job.attempts=10;u.job.action=1;save(&u,"18-policy-review");
    u.screen=UI_WAIT;u.job.op=UI_PASSKEY_DELETE;save(&u,"19-busy-delete");
    u.screen=UI_ERROR;u.error=-3;save(&u,"20-error");
    u.screen=UI_METHOD;u.cursor=0;save(&u,"21-choose-method");
    u.screen=UI_STACK;u.job.count=4;u.cursor=6;
    for(unsigned i=0;i<4;i++)u.job.algorithms[i]=(uint16_t)(i%2+1);
    save(&u,"22-encryption-order");
    u.screen=UI_OPTIONS;u.cursor=0;save(&u,"23-failure-policy");
    u.screen=UI_REVIEW;u.flow=UI_FLOW_SETUP;u.cursor=0;save(&u,"24-create-review");
    u.flow=UI_FLOW_CHANGE;save(&u,"25-change-review");u.flow=UI_FLOW_SETUP;
    u.screen=UI_WAIT;u.job.op=UI_CREATE;u.format_total=1000000;u.format_done=500000;
    u.format_milliseconds=25000;save(&u,"26-formatting");
    fv_ui_fido_begin(&u,false,0,
        "REGISTER login.a-long-subdomain.for-the-preview.example.com");save(&u,"27-long-approval");
    fv_ui_fido_end(&u);u.job.op=UI_STATUS;
    fv_ui_complete(&u,(fv_ui_result){.status=3});save(&u,"28-lockout");
    fv_ui_complete(&u,(fv_ui_result){.status=1});save(&u,"29-welcome");
    u.flipped=true;save(&u,"30-flipped");
    return 0;
}
