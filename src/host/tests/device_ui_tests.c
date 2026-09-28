#include "device_ui.h"
#include "fuse_vault/volume_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void key(fv_ui *u,fv_ui_key k){fv_ui_keypress(u,k);}
static void sequence(fv_ui *u){for(unsigned i=0;i<8;i++)key(u,(fv_ui_key)(1+i%4));key(u,UI_SELECT);}
static void snapshot(fv_ui *u,const char *path){FILE *f=fopen(path,"wb");CHECK(f);fputs("P6\n160 80\n255\n",f);
    for(unsigned i=0;i<FV_SCREEN_BYTES;i++) {
        unsigned v=u->framebuffer[i],r=v>>5,g=(v>>2)&7,b=v&3;
        fputc((r<<5)|(r<<2)|(r>>1),f);fputc((g<<5)|(g<<2)|(g>>1),f);fputc(b*85,f);
    }fclose(f);}
static void flip_tests(void){
    fv_ui a,b;fv_ui_init(&a);fv_ui_complete(&a,(fv_ui_result){.status=2});b=a;
    key(&b,UI_LEFT);CHECK(b.flipped && b.screen==UI_HOME && !b.pending);
    for(unsigned bit=0;bit<12800;bit++){
        unsigned rotated=12799-bit;
        CHECK(a.framebuffer[bit]==b.framebuffer[rotated]);
    }
    key(&b,UI_RIGHT);CHECK(!b.flipped && !memcmp(a.framebuffer,b.framebuffer,FV_SCREEN_BYTES));
    key(&b,UI_RIGHT);key(&a,UI_SELECT);key(&b,UI_SELECT);
    const fv_ui_key directions[]={UI_UP,UI_DOWN,UI_LEFT,UI_RIGHT};
    const fv_ui_key opposite[]={UI_DOWN,UI_UP,UI_RIGHT,UI_LEFT};
    for(unsigned i=0;i<8;i++){key(&a,directions[i%4]);key(&b,opposite[i%4]);}
    CHECK(b.flipped && b.job.length==8 && !memcmp(a.job.secret,b.job.secret,8));
    key(&b,UI_BACK);CHECK(b.job.length==7);key(&b,UI_LEFT);CHECK(b.job.length==8 && b.flipped);
    key(&a,UI_SELECT);key(&b,UI_SELECT);CHECK(a.job.op==UI_UNLOCK && b.job.op==UI_UNLOCK);
    CHECK(!memcmp(a.job.secret,b.job.secret,8));
    fv_ui_complete(&b,(fv_ui_result){.status=2,.unlocked=true});CHECK(b.flipped);
    key(&b,UI_UP);CHECK(b.cursor==1);key(&b,UI_DOWN);CHECK(b.cursor==0);
    key(&b,UI_LEFT);CHECK(!b.flipped && b.device.unlocked && !b.pending);
    key(&b,UI_RIGHT);fv_ui_cancel(&b);CHECK(b.flipped);
    fv_ui_complete(&b,(fv_ui_result){.status=2});CHECK(b.flipped);
    fv_ui_init(&b);CHECK(!b.flipped); /* Orientation is a volatile UI preference. */
}
static void method_tests(void){
    fv_credential_entry e;fv_entry_begin(&e,3);
    CHECK(!fv_entry_key(&e,UI_DOWN) && e.values[0]==99);fv_entry_key(&e,UI_UP);CHECK(e.values[0]==0);
    fv_entry_key(&e,UI_LEFT);CHECK(e.selected==3);fv_entry_key(&e,UI_UP);CHECK(e.values[3]==1);
    CHECK(fv_entry_key(&e,UI_SELECT)==1 && fv_entry_key(&e,UI_BACK)==-1);
    const unsigned directions[]={UI_UP,UI_RIGHT,UI_DOWN,UI_LEFT};
    for(unsigned word=0;word<64;word++){
        fv_entry_begin(&e,4);fv_entry_key(&e,directions[word/16]);fv_entry_key(&e,directions[(word/4)%4]);
        fv_entry_key(&e,directions[word%4]);CHECK(e.count==1 && e.values[0]==word);
        CHECK(!fv_entry_key(&e,UI_SELECT));fv_entry_key(&e,UI_BACK);CHECK(!e.count);
    }
    fv_entry_begin(&e,4);fv_entry_key(&e,UI_LEFT);fv_entry_key(&e,UI_DOWN);fv_entry_key(&e,UI_BACK);CHECK(e.depth==1 && e.prefix==3);
    fv_ui u;fv_ui_init(&u);fv_ui_complete(&u,(fv_ui_result){.status=1});key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);
    CHECK(u.job.profile==3 && u.screen==UI_SECRET);key(&u,UI_UP);key(&u,UI_SELECT);CHECK(u.screen==UI_CONFIRM);
    key(&u,UI_UP);key(&u,UI_SELECT);CHECK(u.screen==UI_STACK && u.job.length==4 && u.job.secret[0]==1);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=4});key(&u,UI_SELECT);CHECK(u.entry.profile==4);
    for(unsigned i=0;i<12;i++)key(&u,UI_UP);
    key(&u,UI_SELECT);CHECK(u.job.op==UI_UNLOCK && u.job.length==4);
    for(unsigned i=0;i<4;i++)CHECK(u.job.secret[i]==0);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true,.attempts=10,.action=1});
    key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_UP);key(&u,UI_SELECT);CHECK(u.flow==UI_FLOW_CHANGE);sequence(&u);CHECK(u.screen==UI_METHOD && u.job.current_length==8);
    key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_SELECT);key(&u,UI_SELECT);CHECK(u.screen==UI_REVIEW);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.op==UI_CHANGE && u.job.profile==3 && u.job.length==4 && u.job.current_length==8);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=3});
    CHECK(!u.job.current_length && !u.job.length);for(unsigned i=0;i<64;i++)CHECK(!u.job.current[i]);
}
static void fido_tests(void){
    fv_ui u;fv_ui_init(&u);fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true});
    u.fido_enabled=true;
    key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_UP);key(&u,UI_UP);key(&u,UI_SELECT);
    CHECK(u.screen==UI_FIDO_POLICY_SCREEN && !u.job.fido_policy);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.pending && u.job.op==UI_FIDO_POLICY && u.job.fido_policy==1);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true,.fido_policy=1});
    u.fido_enabled=true;key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_UP);key(&u,UI_UP);key(&u,UI_UP);key(&u,UI_SELECT);
    CHECK(u.screen==UI_FIDO_INIT_CONFIRM && !u.cursor && !u.pending);
    key(&u,UI_SELECT);CHECK(u.screen==UI_SETTINGS && u.cursor==3 && !u.pending);
    key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);
    CHECK(u.pending && u.job.op==UI_FIDO_INIT);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.profile=2,.unlocked=true});
    fv_ui_fido_begin(&u,false,0,"REGISTER example.test");CHECK(u.fido_modal && !u.fido_done);
    key(&u,UI_BACK);CHECK(u.fido_done && !u.fido_approved);key(&u,UI_SELECT);CHECK(!u.fido_approved);
    fv_ui_fido_end(&u);CHECK(!u.fido_modal && u.pending);
    fv_ui_fido_begin(&u,true,2,"VERIFY");sequence(&u);
    CHECK(u.fido_done && u.fido_approved && u.job.length==8 && !u.pending);
    fv_ui_fido_end(&u);CHECK(!u.job.length && !memcmp(u.job.secret,(uint8_t[64]){0},64));
    fv_ui_fido_begin(&u,false,0,"SIGN IN example.test");CHECK(!u.fido_done);key(&u,UI_SELECT);CHECK(u.fido_approved);
    fv_ui_fido_end(&u);fv_ui_fido_begin(&u,true,3,"VERIFY");key(&u,UI_BACK);CHECK(u.fido_done && !u.fido_approved);
}
static void passkey_tests(void){
    fv_ui u;fv_ui_init(&u);fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true});u.fido_enabled=true;
    key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_UP);key(&u,UI_SELECT);
    CHECK(u.pending && u.job.op==UI_PASSKEY_LIST);
    fv_ui_result r={.status=2,.unlocked=true,.passkey_count=2,.passkey_id={7}};
    strcpy(r.passkey_site,"localhost");strcpy(r.passkey_account,"Alice");
    fv_ui_complete(&u,r);CHECK(u.screen==UI_PASSKEYS && u.job.passkey_id[0]==7);
    unsigned generation=u.fido_generation;
    key(&u,UI_SELECT);CHECK(u.screen==UI_PASSKEY_DELETE_CONFIRM && !u.cursor && u.fido_generation!=generation);
    key(&u,UI_SELECT);CHECK(u.screen==UI_PASSKEYS && !u.pending);
    key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);
    CHECK(u.pending && u.job.op==UI_PASSKEY_DELETE && u.job.passkey_id[0]==7);
    r.passkey_count=0;fv_ui_complete(&u,r);CHECK(u.screen==UI_PASSKEYS);
    key(&u,UI_SELECT);CHECK(u.screen==UI_PASSKEYS && !u.pending);
    key(&u,UI_BACK);CHECK(u.screen==UI_SETTINGS && u.cursor==5);
    key(&u,UI_BACK);CHECK(u.screen==UI_HOME);
}
static void navigation_tests(void){
    fv_ui u;fv_ui_init(&u);u.fido_enabled=true;u.job.op=UI_PASSKEY_LIST;
    fv_ui_result r={.status=2,.unlocked=true,.passkey_count=2,.passkey_id={9}};
    strcpy(r.passkey_site,"github.com");strcpy(r.passkey_account,"Alice");
    fv_ui_complete(&u,r);
    uint8_t site_pixels[3200];memcpy(site_pixels,u.framebuffer+1600,sizeof(site_pixels));
    for(unsigned i=0;i<10;i++)key(&u,UI_RIGHT);
    CHECK(!u.passkey_page); /* Short labels never scroll to an empty page. */
    memset(r.passkey_account,'a',255);r.passkey_account[255]=0;
    fv_ui_complete(&u,r);
    for(unsigned i=0;i<10;i++)key(&u,UI_RIGHT);
    CHECK(u.passkey_page==5);
    CHECK(!memcmp(site_pixels,u.framebuffer+1600,sizeof(site_pixels))); /* Site remains visible. */
    unsigned generation=u.fido_generation;
    key(&u,UI_SELECT);CHECK(u.screen==UI_PASSKEY_DELETE_CONFIRM && !u.cursor && u.passkey_page==5);
    CHECK(u.fido_generation!=generation);
    key(&u,UI_LEFT);CHECK(u.passkey_page==4 && !u.cursor);
    key(&u,UI_DOWN);CHECK(u.cursor==1);
    generation=u.fido_generation;
    key(&u,UI_BACK);CHECK(u.screen==UI_PASSKEYS && !u.pending && !u.cursor && u.passkey_page==4);
    CHECK(u.fido_generation!=generation && u.job.passkey_id[0]==9);
    key(&u,UI_SELECT);CHECK(!u.cursor);key(&u,UI_SELECT);
    CHECK(u.screen==UI_PASSKEYS && u.passkey_page==4);
    key(&u,UI_DOWN);CHECK(u.pending && u.job.passkey_index==1);
    r.passkey_index=1;r.passkey_id[0]=10;fv_ui_complete(&u,r);
    CHECK(!u.passkey_page && u.job.passkey_id[0]==10);
    key(&u,UI_BACK);CHECK(u.screen==UI_SETTINGS && u.cursor==5);
    key(&u,UI_UP);key(&u,UI_SELECT);CHECK(u.screen==UI_FIDO_POLICY_SCREEN);
    key(&u,UI_DOWN);key(&u,UI_BACK);CHECK(u.screen==UI_SETTINGS && u.cursor==4 && !u.pending);
    key(&u,UI_SELECT);CHECK(u.job.fido_policy==u.device.fido_policy);
    key(&u,UI_BACK);key(&u,UI_UP);key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_BACK);
    CHECK(u.screen==UI_SETTINGS && u.cursor==3 && !u.pending);
    key(&u,UI_SELECT);CHECK(!u.cursor); /* Re-entering destruction defaults to Cancel. */
    key(&u,UI_BACK);key(&u,UI_UP);key(&u,UI_UP);key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_BACK);
    CHECK(u.screen==UI_SETTINGS && u.cursor==1 && !u.pending);
}
static void standard_controls_tests(void){
    fv_ui u;fv_ui_init(&u);fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true});
    key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_BACK);
    CHECK(u.screen==UI_HOME && !u.pending);
    key(&u,UI_BACK);CHECK(u.pending && u.job.op==UI_LOCK);
    key(&u,UI_BACK);CHECK(u.job.op==UI_LOCK);
    fv_ui_complete(&u,(fv_ui_result){.status=2});key(&u,UI_BACK);
    CHECK(u.screen==UI_HOME && !u.pending);
    u.screen=UI_OPTIONS;u.cursor=0;u.job.attempts=10;u.job.action=1;
    key(&u,UI_DOWN);CHECK(u.cursor==1 && u.job.attempts==10 && u.job.action==1);
    key(&u,UI_RIGHT);CHECK(u.job.action==2 && u.job.attempts==10);
    key(&u,UI_DOWN);key(&u,UI_RIGHT);CHECK(u.cursor==2 && u.screen==UI_OPTIONS);
    key(&u,UI_SELECT);CHECK(u.screen==UI_REVIEW && !u.cursor && !u.pending);
    u.screen=UI_STACK;u.cursor=0;u.job.count=2;u.job.algorithms[0]=1;u.job.algorithms[1]=1;
    key(&u,UI_LEFT);CHECK(u.job.count==2 && u.job.algorithms[0]==2 && u.job.algorithms[1]==1);
    key(&u,UI_DOWN);key(&u,UI_DOWN);key(&u,UI_DOWN); /* Explicit remove row. */
    key(&u,UI_RIGHT);CHECK(u.job.count==2);
    key(&u,UI_SELECT);CHECK(u.job.count==1 && !u.job.algorithms[1]);
    key(&u,UI_SELECT);CHECK(u.job.count==1); /* Always retain one layer. */
    fv_ui_fido_begin(&u,false,0,"SIGN IN github.com");
    key(&u,UI_UP);key(&u,UI_RIGHT);CHECK(!u.fido_done && !u.fido_approved);
    key(&u,UI_BACK);CHECK(u.fido_done && !u.fido_approved && !u.pending);
}
int main(int argc,char **argv){
    flip_tests();method_tests();fido_tests();passkey_tests();navigation_tests();standard_controls_tests();
    for(uint64_t capacity=0;capacity<100000;capacity++){
        uint64_t n=fv_volume_max_blocks(capacity);
        if(capacity<=2066){CHECK(!n);continue;}
        uint64_t m=n/15+(n%15!=0),b=m/4096+(m%4096!=0);
        CHECK(2064+n+m+b<=capacity);
        ++n;m=n/15+(n%15!=0);b=m/4096+(m%4096!=0);CHECK(2064+n+m+b>capacity);
    }
    CHECK(fv_volume_max_blocks(UINT64_MAX)==UINT32_MAX);
    CHECK(fv_volume_max_blocks(67108864)==62911665); /* 32 GiB physical card */
    fv_ui u;fv_ui_init(&u);CHECK(u.pending && u.job.op==UI_STATUS);
    fv_ui_complete(&u,(fv_ui_result){.status=1});key(&u,UI_SELECT);CHECK(u.screen==UI_METHOD);key(&u,UI_SELECT);sequence(&u);
    CHECK(u.screen==UI_CONFIRM);key(&u,UI_UP);key(&u,UI_SELECT);
    CHECK(u.screen==UI_SECRET && !u.job.length);for(unsigned i=0;i<64;i++)CHECK(!u.job.secret[i]);
    sequence(&u);sequence(&u);CHECK(u.screen==UI_STACK);
    key(&u,UI_SELECT);CHECK(u.job.algorithms[0]==2);
    key(&u,UI_SELECT);CHECK(u.job.algorithms[0]==3);
    key(&u,UI_SELECT);CHECK(u.job.algorithms[0]==1);
    key(&u,UI_SELECT);CHECK(u.job.algorithms[0]==2);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.count==2);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.count==3);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.count==4);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.count==4);
    key(&u,UI_DOWN);key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.screen==UI_OPTIONS);
    for(unsigned i=0;i<150;i++){key(&u,UI_RIGHT);}CHECK(u.job.attempts==100);
    for(unsigned i=0;i<150;i++){key(&u,UI_LEFT);}CHECK(u.job.attempts==1);
    for(unsigned i=0;i<9;i++)key(&u,UI_RIGHT);
    key(&u,UI_DOWN);key(&u,UI_RIGHT);CHECK(u.job.action==2);key(&u,UI_DOWN);key(&u,UI_SELECT);
    CHECK(u.screen==UI_REVIEW && !u.pending);
    if(argc>1)snapshot(&u,argv[1]);
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.pending && u.job.op==UI_CREATE && u.job.length==8);
    key(&u,UI_SELECT);CHECK(u.job.op==UI_CREATE); /* No double submission while working. */
    fv_ui cancelled=u;fv_ui_cancel(&cancelled);CHECK(cancelled.job.op==UI_STATUS && !cancelled.job.length);
    u.pending=false;fv_ui_cancel(&u);CHECK(u.job.op==UI_CREATE && !u.pending && !u.job.length); /* Accepted job cannot be resubmitted. */
    fv_ui_complete(&u,(fv_ui_result){.status=2});CHECK(!u.job.length);for(unsigned i=0;i<64;i++)CHECK(!u.job.secret[i]);
    key(&u,UI_SELECT);sequence(&u);CHECK(u.job.op==UI_UNLOCK);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true,.attempts=10,.action=1,.blocks=62912625});
    key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.screen==UI_SETTINGS && !u.pending);key(&u,UI_BACK);CHECK(u.screen==UI_HOME && !u.pending);key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_SELECT);CHECK(u.flow==UI_FLOW_POLICY && u.screen==UI_SECRET);
    sequence(&u);CHECK(u.screen==UI_OPTIONS);key(&u,UI_DOWN);key(&u,UI_RIGHT);key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.op==UI_POLICY);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true});key(&u,UI_DOWN);key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);
    CHECK(u.screen==UI_ERASE_CONFIRM && !u.pending);key(&u,UI_SELECT);CHECK(u.screen==UI_SETTINGS && u.cursor==1 && !u.pending);
    key(&u,UI_SELECT);key(&u,UI_DOWN);key(&u,UI_SELECT);CHECK(u.job.op==UI_ERASE);
    fv_ui_complete(&u,(fv_ui_result){.status=3});key(&u,UI_SELECT);CHECK(u.screen==UI_HOME && !u.pending);
    fv_ui_complete(&u,(fv_ui_result){.status=2});key(&u,UI_SELECT);key(&u,UI_UP);fv_ui_cancel(&u);
    CHECK(u.job.length==0 && u.job.op==UI_STATUS);for(unsigned i=0;i<64;i++)CHECK(!u.job.secret[i]);
    fv_ui_init(&u);u.job.op=UI_CREATE;u.format_total=7681727;u.format_done=3840863;u.format_milliseconds=300000;
    fv_ui_render(&u);
    unsigned filled=35*160+30,empty=35*160+130;
    CHECK(u.framebuffer[filled]);
    CHECK(!(u.framebuffer[empty]));
    if(argc>2)snapshot(&u,argv[2]);
    u.format_done=u.format_total;fv_ui_render(&u);CHECK(u.screen==UI_WAIT);
    puts("UI setup, confirmation, policy, lockout, cancellation, progress and capacity checks passed");return 0;
}
