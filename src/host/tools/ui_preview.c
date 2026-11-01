/* Offline visual fixtures, rendered by the actual device UI. No vault or USB. */
#include "device_ui.h"
#include "drawing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(fv_ui *u,const char *name){
    char path[96];snprintf(path,sizeof(path),"%s.ppm",name);
    if (!strcmp(name,"00-startup")) ui_draw_splash(u, 9);
    else fv_ui_render(u);
    FILE *f=fopen(path,"wb");
    if(!f){perror(path);exit(1);}
    fputs("P6\n160 80\n255\n",f);
    for(unsigned i=0;i<FV_SCREEN_BYTES;i++) {
        unsigned v=u->framebuffer[i],r=v>>5,g=(v>>2)&7,b=v&3;
        fputc((r<<5)|(r<<2)|(r>>1),f);fputc((g<<5)|(g<<2)|(g>>1),f);fputc(b*85,f);
    }
    if(ferror(f) || fclose(f))exit(1);
}
int main(void){
    fv_ui u;fv_ui_init(&u);u.fido_enabled=true;
    save(&u,"00-startup");
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.attempts=10,.action=1});
    save(&u,"01-locked");
    {
        fv_ui locked = u;
        locked.flipped = true; save(&locked, "01a-locked-flipped");
        locked.flipped = false; locked.fido_enabled = false;
        save(&locked, "01b-locked-files-only");
        locked.flipped = true; save(&locked, "01c-locked-files-only-flipped");
    }
    fv_ui_keypress(&u,UI_SELECT);
    save(&u,"02a-pattern-empty");
    for(unsigned i=0;i<8;i++)fv_ui_keypress(&u,(fv_ui_key)(i%4+1));
    save(&u,"02-pattern");
    {
        fv_ui pattern = u;
        for(unsigned i=8;i<64;i++)fv_ui_keypress(&pattern,(fv_ui_key)(i%4+1));
        save(&pattern,"02b-pattern-full");
        pattern.screen=UI_CONFIRM;pattern.confirmation_length=8;
        memcpy(pattern.confirmation,pattern.job.secret,8);
        save(&pattern,"02c-pattern-confirm");
        pattern.screen=UI_SECRET;pattern.job.length=0;pattern.error=1;
        save(&pattern,"02d-pattern-mismatch");
        pattern.job.length=3;pattern.error=2;
        save(&pattern,"02e-pattern-too-short");
        pattern.fido_modal=true;pattern.error=0;
        save(&pattern,"02f-pattern-fido");
    }
    fv_ui_fido_begin(&u,true,3,"VERIFY");save(&u,"03-code-wheels");
    {
        fv_ui wheels=u;wheels.entry.selected=2;
        wheels.entry.values[0]=99;wheels.entry.values[1]=12;
        wheels.entry.values[2]=34;wheels.entry.values[3]=56;
        save(&wheels,"03a-wheels-selected");
        wheels.error=1;save(&wheels,"03b-wheels-mismatch");
    }
    fv_ui_fido_begin(&u,true,4,"VERIFY");save(&u,"04-word-entry");
    {
        fv_ui words=u;
        words.entry.depth=1;words.entry.prefix=3;save(&words,"04a-word-group");
        words.entry.depth=2;words.entry.prefix=15;save(&words,"04b-word-leaves");
        words.entry.count=4;words.entry.depth=0;words.entry.prefix=0;
        words.entry.values[0]=0;words.entry.values[1]=57;
        words.entry.values[2]=61;words.entry.values[3]=63;
        save(&words,"04c-word-review");
        words.entry.count=0;words.error=1;save(&words,"04d-word-mismatch");
    }
    fv_ui_fido_end(&u);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true,.mode=FV_ACCESS_FULL,.blocks=115000000,.attempts=10,.action=1});
    save(&u,"05a-history-idle");
    for(unsigned i=0;i<60;i++)
        fv_ui_activity(&u, (i%17)*27000, (i%23)*38000, 500000);
    save(&u,"05b-history");
    u.dashboard_bars=true;save(&u,"05c-live-bars");
    fv_ui_keypress(&u,UI_SELECT);save(&u,"06-settings");
    fv_ui_keypress(&u,UI_UP);fv_ui_keypress(&u,UI_SELECT);save(&u,"05d-dashboard-setting");
    fv_ui_keypress(&u,UI_BACK);fv_ui_keypress(&u,UI_UP);save(&u,"07-settings-bottom");
    fv_ui_keypress(&u,UI_UP);fv_ui_keypress(&u,UI_SELECT);save(&u,"08-verification-timed");
    fv_ui_keypress(&u,UI_DOWN);save(&u,"09-verification-session");
    fv_ui_fido_begin(&u,false,0,"SIGN IN github.com");save(&u,"10-approval");
    fv_ui_fido_end(&u);u.job.op=UI_PASSKEY_LIST;
    fv_ui_result r={.status=2,.profile=2,.unlocked=true,.mode=FV_ACCESS_FULL,.passkey_count=2,.passkey_id={1}};
    strcpy(r.passkey_site,"github.com");strcpy(r.passkey_account,"alice@example.com");
    fv_ui_complete(&u,r);save(&u,"11-passkey");
    fv_ui_keypress(&u,UI_SELECT);save(&u,"12-delete");
    strcpy(r.passkey_site,"login.a-long-subdomain.for-the-preview.example.com");
    strcpy(r.passkey_account,"alice.with-a-long-account-name+test-account@example.com");
    fv_ui_complete(&u,r);fv_ui_keypress(&u,UI_RIGHT);save(&u,"13-long-label-tail");
    fv_ui_keypress(&u,UI_SELECT);save(&u,"14-delete-tail");
    r.passkey_count=0;fv_ui_complete(&u,r);save(&u,"15-empty-passkeys");
    u.screen=UI_ERASE_CONFIRM;u.cursor=0;save(&u,"16-destroy");
    u.cursor=1;save(&u,"16a-destroy-selected");
    u.cursor=0;u.screen=UI_FIDO_INIT_CONFIRM;save(&u,"17-initialize-fido");
    u.screen=UI_REVIEW;u.flow=UI_FLOW_POLICY;u.job.attempts=10;u.job.action=1;u.job.auto_lock_seconds=300;save(&u,"18-policy-review");
    u.screen=UI_WAIT;u.job.op=UI_PASSKEY_DELETE;save(&u,"19-busy-delete");
    u.job.op=UI_UNLOCK;
    for(unsigned i=0;i<12;i++) {
        char name[40];snprintf(name,sizeof(name),"19-unlock-%02u",i);
        u.busy_frame=i;save(&u,name);
    }
    u.screen=UI_ERROR;u.error=-3;save(&u,"20-error");
    u.screen=UI_METHOD;u.cursor=0;save(&u,"21-choose-method");
    u.screen=UI_STACK;u.job.count=4;u.cursor=6;
    for(unsigned i=0;i<4;i++)u.job.algorithms[i]=(uint16_t)(i%2+1);
    save(&u,"22-encryption-order");
    u.screen=UI_OPTIONS;u.cursor=0;save(&u,"23-security");
    u.cursor=1;save(&u,"23a-failed-attempts");
    u.cursor=2;save(&u,"23b-auto-lock");
    u.cursor=3;save(&u,"23e-screen-until-lock");
    u.job.screen_sleep_seconds=UINT16_MAX;save(&u,"23f-screen-never");
    u.job.screen_sleep_seconds=30;save(&u,"23g-screen-timed");
    u.cursor=4;save(&u,"23h-security-review-row");
    u.job.screen_sleep_seconds=0;
    u.job.action=5;u.job.attempts=0;u.cursor=0;save(&u,"23c-destruction-off");
    u.screen=UI_REVIEW;u.flow=UI_FLOW_POLICY;save(&u,"23d-security-off-review");
    u.job.action=1;u.job.attempts=100;
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
    {
        const fv_access_mode modes[] = {FV_ACCESS_FULL, FV_ACCESS_PASSKEYS_ONLY, FV_ACCESS_FILES_READ_ONLY};
        const fv_ui_key choices[] = {UI_SELECT, UI_UP, UI_DOWN};
        for (unsigned mode = 0; mode < 3; ++mode) {
            char name[64];
            fv_ui_init(&u); u.fido_enabled = true;
            fv_ui_complete(&u, (fv_ui_result){.status=2, .profile=3});
            fv_ui_keypress(&u, choices[mode]);
            snprintf(name, sizeof(name), "31-mode-%u-credential", mode); save(&u, name);
            fv_ui_complete(&u, (fv_ui_result){.status=2, .profile=3, .unlocked=true, .mode=modes[mode]});
            snprintf(name, sizeof(name), "32-mode-%u-dashboard", mode); save(&u, name);
            fv_ui_keypress(&u, UI_SELECT);
            snprintf(name, sizeof(name), "33-mode-%u-settings", mode); save(&u, name);
        }
    }
    {
        const fv_access_mode modes[] = {FV_ACCESS_FULL, FV_ACCESS_PASSKEYS_ONLY, FV_ACCESS_FILES_READ_ONLY};
        const unsigned seconds[] = {1800, 900, 61, 30, 10, 0};
        for (unsigned mode = 0; mode < 3; ++mode) {
            fv_ui_init(&u);
            fv_ui_complete(&u, (fv_ui_result){.status=2, .unlocked=true,
                .mode=modes[mode], .auto_lock_seconds=1800});
            for (unsigned i = 0; i < sizeof(seconds)/sizeof(seconds[0]); ++i) {
                char name[64];
                fv_ui_countdown(&u, seconds[i]);
                snprintf(name, sizeof(name), "34-countdown-%u-%02u", mode, i); save(&u, name);
            }
        }
    }
    return 0;
}
