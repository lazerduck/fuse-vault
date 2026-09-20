#include "device_ui.h"
#include <stdio.h>
#include <string.h>
/* Bitmap font reused from the archived V1 renderer. */
static uint8_t glyph_row(char character, unsigned row) {
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };

    if (row >= 7u) return 0u;
    if (character >= 'a' && character <= 'z') character -= (char)('a' - 'A');
    if (character >= 'A' && character <= 'Z') return letters[character - 'A'][row];
    if (character >= '0' && character <= '9') return digits[character - '0'][row];
    switch (character) {
        case '\001': { static const uint8_t g[7] = {4,2,1,31,1,2,4}; return g[row]; }
        case '\002': { static const uint8_t g[7] = {4,4,4,21,14,4,0}; return g[row]; }
        case '\003': { static const uint8_t g[7] = {4,8,16,31,16,8,4}; return g[row]; }
        case '\004': { static const uint8_t g[7] = {0,4,14,21,4,4,4}; return g[row]; }
        case '\005': { static const uint8_t g[7] = {8,16,31,17,1,1,14}; return g[row]; }
        case '\006': { static const uint8_t g[7] = {1,1,5,9,31,8,4}; return g[row]; }
        case '>': { static const uint8_t g[7] = {16,8,4,2,4,8,16}; return g[row]; }
        case '<': { static const uint8_t g[7] = {1,2,4,8,4,2,1}; return g[row]; }
        case '[': { static const uint8_t g[7] = {14,8,8,8,8,8,14}; return g[row]; }
        case ']': { static const uint8_t g[7] = {14,2,2,2,2,2,14}; return g[row]; }
        case '@': { static const uint8_t g[7] = {14,17,23,21,23,16,14}; return g[row]; }
        case '?': { static const uint8_t g[7] = {14,17,1,2,4,0,4}; return g[row]; }
        case '_': return row == 6u ? 31u : 0u;
        case '+': return row == 3u ? 31u : (row >= 1u && row <= 5u ? 4u : 0u);
        case ':': return (row == 2u || row == 5u) ? 4u : 0u;
        case '/': return (uint8_t)(1u << (row < 5u ? row : 4u));
        case '-': return row == 3u ? 14u : 0u;
        case '.': return row == 6u ? 4u : 0u;
        case ' ': return 0u;
        default: return glyph_row('?', row);
    }
}

void fv_ui_wipe(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static void line(fv_ui *u,unsigned row,const char *s){
    for(unsigned x=2;*s && x+5<=160;x+=6,s++)for(unsigned y=0;y<7;y++)
        for(unsigned b=0;b<5;b++)if(glyph_row(*s,y)&(16u>>b)){
            unsigned bit=(row*10+y)*160+x+b;
            u->framebuffer[bit/8]|=0x80u>>(bit%8);
        }
}
/* Pixel-positioned credential controls share the same framebuffer/flip path. */
static void pixel(fv_ui *u,unsigned x,unsigned y,bool on){
    if(x>=160 || y>=80)return;
    unsigned bit=y*160+x;uint8_t mask=(uint8_t)(0x80u>>(bit%8));
    if(on)u->framebuffer[bit/8]|=mask;else u->framebuffer[bit/8]&=(uint8_t)~mask;
}
static void text_at(fv_ui *u,unsigned x,unsigned y,const char *s,unsigned scale,bool ink){
    for(;*s;s++,x+=6*scale)for(unsigned r=0;r<7;r++)for(unsigned c=0;c<5;c++)
        if(glyph_row(*s,r)&(16u>>c))for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++)
            pixel(u,x+c*scale+dx,y+r*scale+dy,ink);
}
static void rule(fv_ui *u,unsigned y){for(unsigned x=3;x<157;x++)pixel(u,x,y,true);}
static void credential_controls(fv_ui *u){
    const fv_credential_entry *e=&u->entry;char label[27];
    rule(u,10);rule(u,67);
    text_at(u,4,71,"\005 BACK",1,true);
    if(e->profile==3 || e->count==4)text_at(u,118,71,"\006 DONE",1,true);
    if(e->profile==3){
        for(unsigned i=0;i<4;i++){
            unsigned x=5+i*39;
            snprintf(label,sizeof(label),"%02u",(e->values[i]+1)%100);text_at(u,x+11,18,label,1,true);
            if(i==e->selected)for(unsigned y=30;y<50;y++)for(unsigned dx=0;dx<34;dx++)pixel(u,x+dx,y,true);
            snprintf(label,sizeof(label),"%02u",e->values[i]);text_at(u,x+6,33,label,2,i!=e->selected);
            snprintf(label,sizeof(label),"%02u",(e->values[i]+99)%100);text_at(u,x+11,55,label,1,true);
        }
    }else{
        if(e->count==4)snprintf(label,sizeof(label),"REVIEW WORDS");
        else snprintf(label,sizeof(label),"WORD %u/4",e->count+1);
        text_at(u,(160-(unsigned)strlen(label)*6)/2,14,label,1,true);
        for(unsigned i=0;i<4;i++){
            if(e->count==4)snprintf(label,sizeof(label),"%u %s",i+1,fv_entry_word(e->values[i]));
            else{
                unsigned size=16u>>(2*e->depth),start=(e->prefix*4u+i)*size;
                if(size==1)snprintf(label,sizeof(label),"%s",fv_entry_word(start));
                else snprintf(label,sizeof(label),"%.3s-%.3s",fv_entry_word(start),fv_entry_word(start+size-1));
            }
            unsigned width=(unsigned)strlen(label)*6-1;
            unsigned x=i==3?4:i==1?156-width:(160-width)/2;
            unsigned y=i==0?27:i==2?55:41;
            text_at(u,x,y,label,1,true);
        }
        text_at(u,77,41,"+",1,true);
    }
    if(u->error==1){
        for(unsigned y=11;y<25;y++)for(unsigned x=0;x<160;x++)pixel(u,x,y,false);
        text_at(u,14,15,"MISMATCH - TRY AGAIN",1,true);
    }
}
static void submit(fv_ui *u,fv_ui_operation op){u->format_done=u->format_total=u->format_milliseconds=0;u->job.op=op;u->pending=true;u->screen=UI_WAIT;}
static void clear_input(fv_ui *u){u->settings=false;u->changing=false;fv_ui_wipe(u->job.secret,64);u->job.length=0;fv_ui_wipe(u->job.current,64);u->job.current_length=0;fv_ui_wipe(u->confirmation,64);u->confirmation_length=0;fv_ui_wipe(&u->entry,sizeof(u->entry));}
void fv_ui_init(fv_ui *u){memset(u,0,sizeof(*u));submit(u,UI_STATUS);fv_ui_render(u);}
void fv_ui_cancel(fv_ui *u){clear_input(u);u->settings=false;u->changing=false;u->cursor=0;if(u->screen!=UI_WAIT || u->pending)submit(u,UI_STATUS);fv_ui_render(u);}
void fv_ui_complete(fv_ui *u,fv_ui_result r){
    clear_input(u);u->pending=false;u->device=r;u->cursor=0;u->settings=false;u->changing=false;
    u->screen=r.result?UI_ERROR:UI_HOME;u->error=r.result;fv_ui_render(u);
}
void fv_ui_keypress(fv_ui *u,fv_ui_key k){
    if(k<UI_UP || k>UI_BACK || u->screen==UI_WAIT)return;
    if(u->flipped){
        if(k==UI_UP)k=UI_DOWN;else if(k==UI_DOWN)k=UI_UP;
        else if(k==UI_LEFT)k=UI_RIGHT;else if(k==UI_RIGHT)k=UI_LEFT;
    }
    /* Home only: orientation can never change halfway through a credential. */
    if(u->screen==UI_HOME && (k==UI_LEFT || k==UI_RIGHT)){
        u->flipped=!u->flipped;goto done;
    }
    if(u->screen==UI_ERROR){if(k==UI_SELECT || k==UI_BACK)submit(u,UI_STATUS);goto done;}
    if(u->screen==UI_SECRET || u->screen==UI_CONFIRM){
        uint8_t *p=u->screen==UI_SECRET?u->job.secret:u->confirmation;
        uint8_t *n=u->screen==UI_SECRET?&u->job.length:&u->confirmation_length;
        bool submitted=false;
        if(u->job.profile==3 || u->job.profile==4){
            if(k<=UI_RIGHT)u->error=0;
            int result=fv_entry_key(&u->entry,k);
            if(result<0){clear_input(u);u->screen=UI_HOME;goto done;}
            if(result>0){u->error=0;memcpy(p,u->entry.values,4);*n=4;submitted=true;}
        }else {
            if(k<=UI_RIGHT){u->error=0;if(*n<64)p[(*n)++]=(uint8_t)k;}
            else if(k==UI_BACK){if(*n)p[--*n]=0;else {clear_input(u);u->screen=UI_HOME;}}
            else if(*n)submitted=true;
        }
        if(submitted){
            if(u->changing && !u->job.current_length){
                memcpy(u->job.current,u->job.secret,u->job.length);u->job.current_length=u->job.length;
                fv_ui_wipe(u->job.secret,64);u->job.length=0;fv_ui_wipe(&u->entry,sizeof(u->entry));
                u->screen=UI_METHOD;u->cursor=0;
            }else if(u->settings){fv_ui_wipe(&u->entry,sizeof(u->entry));u->screen=UI_OPTIONS;}
            else if(u->device.status==2 && !u->changing){submit(u,UI_UNLOCK);}
            else if(u->screen==UI_SECRET){
                if(u->job.profile!=2 || *n>=8){u->screen=UI_CONFIRM;fv_entry_begin(&u->entry,u->job.profile);}
                else u->error=2;
            }else if(u->job.length==*n && !memcmp(u->job.secret,p,*n)){
                fv_ui_wipe(u->confirmation,64);u->confirmation_length=0;fv_ui_wipe(&u->entry,sizeof(u->entry));u->screen=u->changing?UI_REVIEW:UI_STACK;u->cursor=0;
            }else {
                fv_ui_wipe(u->job.secret,64);u->job.length=0;fv_ui_wipe(u->confirmation,64);u->confirmation_length=0;
                fv_entry_begin(&u->entry,u->job.profile);u->error=1;u->screen=UI_SECRET;
            }
        }
        goto done;
    }
    if(k==UI_BACK){clear_input(u);u->settings=false;u->changing=false;u->screen=UI_HOME;u->cursor=0;goto done;}
    switch(u->screen){
    case UI_HOME:
        if(u->device.unlocked){
            if(k==UI_UP || k==UI_DOWN)u->cursor^=1;
            if(k==UI_SELECT){
                if(!u->cursor)submit(u,UI_LOCK);
                else {u->screen=UI_SETTINGS;u->cursor=0;}
            }
        }else if(k==UI_SELECT && (u->device.status==0 || u->device.status==1 || u->device.status==2 || u->device.status==5)){
            memset(&u->job,0,sizeof(u->job));u->error=0;u->job.count=1;u->job.algorithms[0]=1;u->job.attempts=10;u->job.action=1;
            u->job.profile=u->device.profile?u->device.profile:2;
            if(u->device.status==2){
                if(u->job.profile==1){u->error=-6;u->screen=UI_ERROR;}
                else {fv_entry_begin(&u->entry,u->job.profile);u->screen=UI_SECRET;}
            }else {u->cursor=0;u->screen=UI_METHOD;}
        }
        break;
    case UI_SETTINGS:
        if(k==UI_UP)u->cursor=(u->cursor+2)%3;
        if(k==UI_DOWN)u->cursor=(u->cursor+1)%3;
        if(k==UI_SELECT){
                if(u->cursor==0){clear_input(u);u->settings=true;u->job.attempts=u->device.attempts;u->job.action=u->device.action;u->job.profile=u->device.profile?u->device.profile:2;fv_entry_begin(&u->entry,u->job.profile);u->screen=UI_SECRET;}
                else if(u->cursor==1){u->cursor=0;u->screen=UI_ERASE_CONFIRM;}
                else {clear_input(u);u->changing=true;u->settings=false;u->job.profile=u->device.profile?u->device.profile:2;
                    u->job.attempts=u->device.attempts;u->job.action=u->device.action;
                    fv_entry_begin(&u->entry,u->job.profile);u->screen=UI_SECRET;}
        }
        break;
    case UI_METHOD:
        if(k==UI_UP)u->cursor=(u->cursor+2)%3;
        if(k==UI_DOWN)u->cursor=(u->cursor+1)%3;
        if(k==UI_SELECT){u->job.profile=(uint16_t)(u->cursor+2);fv_entry_begin(&u->entry,u->job.profile);u->screen=UI_SECRET;}
        break;
    case UI_STACK:{
        unsigned last=u->job.count+1;
        if(k==UI_UP)u->cursor=u->cursor?u->cursor-1:last;
        if(k==UI_DOWN)u->cursor=(u->cursor+1)%(last+1);
        if(k==UI_LEFT && u->job.count>1){u->job.algorithms[--u->job.count]=0;u->cursor=0;}
        if(k==UI_SELECT || k==UI_RIGHT){
            if(u->cursor<u->job.count)u->job.algorithms[u->cursor]=u->job.algorithms[u->cursor]==1?2:1;
            else if(u->cursor==u->job.count){if(u->job.count<4)u->job.algorithms[u->job.count++]=1;}
            else u->screen=UI_OPTIONS;
        }
        break;}
    case UI_OPTIONS:
        if(k==UI_UP && u->job.attempts<100)u->job.attempts++;
        if(k==UI_DOWN && u->job.attempts>1)u->job.attempts--;
        if(k==UI_LEFT || k==UI_RIGHT)u->job.action=u->job.action==1?2:1;
        if(k==UI_SELECT){u->cursor=0;u->screen=UI_REVIEW;}
        break;
    case UI_REVIEW:
    case UI_ERASE_CONFIRM:
        if(k==UI_UP || k==UI_DOWN)u->cursor^=1;
        if(k==UI_SELECT){
            if(!u->cursor){clear_input(u);u->screen=UI_HOME;}
            else submit(u,u->screen==UI_ERASE_CONFIRM?UI_ERASE:u->changing?UI_CHANGE:u->settings?UI_POLICY:UI_CREATE);
        }
        break;
    default:break;
    }
 done:fv_ui_render(u);
}
void fv_ui_render(fv_ui *u){
    memset(u->framebuffer,0,sizeof(u->framebuffer));char b[32];
    switch(u->screen){
    case UI_WAIT:
        line(u,0,"FUSE VAULT");
        if(u->job.op==UI_CREATE && u->format_total){
            uint32_t done=u->format_done>u->format_total?u->format_total:u->format_done;
            unsigned percent=(unsigned)((uint64_t)done*100/u->format_total);
            line(u,1,done==u->format_total?"FINALIZING VAULT":"INITIALIZING BITMAP");
            if(u->format_total<2048)snprintf(b,sizeof(b),"%u%%  %lu / %lu KIB",percent,(unsigned long)(done/2),(unsigned long)((u->format_total+1)/2));
            else snprintf(b,sizeof(b),"%u%%  %lu / %lu MIB",percent,(unsigned long)(done/2048),(unsigned long)(u->format_total/2048));
            line(u,2,b);
            unsigned filled=(unsigned)((uint64_t)done*154/u->format_total);
            for(unsigned y=32;y<40;y++)for(unsigned x=2;x<158;x++)
                if(y==32 || y==39 || x==2 || x==157 || x<3+filled){unsigned bit=y*160+x;u->framebuffer[bit/8]|=0x80u>>(bit%8);}
            unsigned rate=u->format_milliseconds?(unsigned)((uint64_t)done*5000/u->format_milliseconds):0;
            snprintf(b,sizeof(b),"%u.%u KIB/S  %lus",rate/10,rate%10,(unsigned long)(u->format_milliseconds/1000));line(u,5,b);
        }else {line(u,2,"WORKING - PLEASE WAIT");line(u,4,u->job.op==UI_CREATE?"PREPARING VAULT KEYS":"SECURE OPERATION");}
        line(u,7,"KEEP POWER CONNECTED");break;
    case UI_HOME:
        text_at(u,4,2,"FUSE VAULT",1,true);
        text_at(u,112,2,u->device.unlocked?"OPEN":"LOCKED",1,true);
        rule(u,12);
        if(u->device.unlocked){
            uint64_t tenths=u->device.blocks*10/2097152;
            snprintf(b,sizeof(b),"%llu.%llu GIB VAULT",(unsigned long long)(tenths/10),(unsigned long long)(tenths%10));
            text_at(u,4,17,b,1,true);
            const char *items[]={"LOCK VAULT","SETTINGS"};
            for(unsigned i=0;i<2;i++){
                unsigned y=34+i*15;bool selected=u->cursor==i;
                if(selected)for(unsigned r=y;r<y+9;r++)for(unsigned x=3;x<157;x++)pixel(u,x,r,true);
                text_at(u,7,y+1,items[i],1,!selected);
            }
        }else{
            const char *title=u->device.status==2?"VAULT LOCKED":(u->device.status==3 || u->device.status==4)?"ACCESS DISABLED":"WELCOME";
            text_at(u,(160-(unsigned)strlen(title)*6)/2,29,title,1,true);
            const char *action=u->device.status==2?"\006 UNLOCK":(u->device.status==3 || u->device.status==4)?"ATTEMPT LIMIT REACHED":"\006 SET UP VAULT";
            text_at(u,(160-(unsigned)strlen(action)*6)/2,48,action,1,true);
        }
        rule(u,67);
        text_at(u,4,71,"\003\001 FLIP",1,true);
        if(u->device.unlocked)text_at(u,76,71,"EJECT FIRST",1,true);
        break;
    case UI_SETTINGS:{
        text_at(u,4,2,"SETTINGS",1,true);rule(u,12);
        const char *items[]={"FAILURE POLICY","ERASE AND SET UP","UNLOCK METHOD"};
        for(unsigned i=0;i<3;i++){
            unsigned y=23+i*13;bool selected=u->cursor==i;
            if(selected)for(unsigned r=y;r<y+11;r++)for(unsigned x=3;x<157;x++)pixel(u,x,r,true);
            text_at(u,7,y+2,items[i],1,!selected);
        }
        rule(u,67);text_at(u,4,71,"\005 BACK",1,true);text_at(u,76,71,"EJECT FIRST",1,true);
        break;}
    case UI_METHOD:
        line(u,0,"CHOOSE UNLOCK METHOD");
        line(u,2,u->cursor==0?"> DIRECTION PATTERN":"  DIRECTION PATTERN");
        line(u,3,u->cursor==1?"> FOUR CODE WHEELS":"  FOUR CODE WHEELS");
        line(u,4,u->cursor==2?"> FOUR WORDS":"  FOUR WORDS");
        line(u,6,"UP/DOWN: CHOOSE");line(u,7,"SELECT: CONTINUE");break;
    case UI_SECRET:case UI_CONFIRM:
        line(u,0,u->screen==UI_CONFIRM?"CONFIRM CREDENTIAL":(u->settings || (u->changing && !u->job.current_length))?"CURRENT CREDENTIAL":(u->device.status==2 && !u->changing)?"UNLOCK VAULT":"NEW CREDENTIAL");
        if(u->job.profile==3 || u->job.profile==4){
            credential_controls(u);
        }else{
            if(u->error==1)line(u,1,"MISMATCH - START AGAIN");
            if(u->error==2)line(u,1,"USE AT LEAST 8 INPUTS");
            unsigned n=u->screen==UI_CONFIRM?u->confirmation_length:u->job.length;
            const uint8_t *p=u->screen==UI_CONFIRM?u->confirmation:u->job.secret;
            const char arrows[]={0,4,2,3,1};
            snprintf(b,sizeof(b),"%u INPUTS",n);line(u,2,b);
            for(unsigned row=0;row<3;row++){
                unsigned j=0;
                while(row*22+j<n && j<22){b[j]=p[row*22+j]<=4?arrows[p[row*22+j]]:'?';j++;}
                b[j]=0;line(u,row+3,b);
            }
            line(u,6,"SELECT: DONE  BACK: DELETE");line(u,7,"NEW: 8-64 DIRECTIONS");
        }
        break;
    case UI_STACK:
        line(u,0,"ENCRYPTION ORDER");
        for(unsigned i=0;i<u->job.count;i++){snprintf(b,sizeof(b),"%c %u %s",u->cursor==i?'>':' ',i+1,u->job.algorithms[i]==1?"AES-256":"CAMELLIA-256");line(u,i+1,b);}
        line(u,u->job.count+1,u->cursor==u->job.count?"> ADD LAYER (MAX 4)":"  ADD LAYER (MAX 4)");line(u,u->job.count+2,u->cursor==u->job.count+1u?"> CONTINUE":"  CONTINUE");line(u,7,"LEFT REMOVES LAST LAYER");break;
    case UI_OPTIONS:
        line(u,0,"FAILURE POLICY");snprintf(b,sizeof(b),"ATTEMPTS: %u",(unsigned)u->job.attempts);line(u,2,b);
        line(u,3,u->job.action==1?"THEN DESTROY VAULT KEY":"THEN PERMANENT LOCKOUT");line(u,5,"UP/DOWN: ATTEMPTS 1-100");line(u,6,"LEFT/RIGHT: ACTION");line(u,7,"SELECT TO REVIEW");break;
    case UI_REVIEW:
        line(u,0,u->changing?"CHANGE UNLOCK METHOD?":u->settings?"APPLY FAILURE POLICY?":"CREATE FULL SD VAULT?");
        line(u,1,u->changing?"PRESERVES FILES AND VMK":u->settings?"REQUIRES CURRENT CREDENTIAL":"ERASES EXISTING SD DATA");
        snprintf(b,sizeof(b),"%u FAILURES: %s",(unsigned)u->job.attempts,u->job.action==1?"DESTROY":"LOCKOUT");line(u,3,b);
        if(!u->settings && !u->changing){snprintf(b,sizeof(b),"%u LAYERS / KDF 60000",u->job.count);line(u,4,b);}
        line(u,6,!u->cursor?"> CANCEL":"  CANCEL");line(u,7,u->cursor?"> CONFIRM":"  CONFIRM");break;
    case UI_ERASE_CONFIRM:
        line(u,0,"DESTROY CURRENT VAULT?");line(u,2,"ALL FILES BECOME LOST");line(u,3,"USES NEXT OTP TOKEN");line(u,4,"UNMOUNT DRIVE FIRST");line(u,6,!u->cursor?"> CANCEL":"  CANCEL");line(u,7,u->cursor?"> DESTROY":"  DESTROY");break;
    case UI_ERROR:
        line(u,0,"OPERATION FAILED");snprintf(b,sizeof(b),"RESULT %d",u->error);line(u,2,b);line(u,4,u->error==-6?"USE LEGACY DEBUG UNLOCK":"CHECK CREDENTIAL OR SD");line(u,6,"SELECT TO RETURN");break;
    }
    if(u->flipped){
        /* A 180-degree rotation reverses both byte order and bit order. */
        for(unsigned i=0;i<FV_SCREEN_BYTES/2;i++){
            uint8_t a=u->framebuffer[i],b=u->framebuffer[FV_SCREEN_BYTES-1-i];
            uint8_t ra=0,rb=0;
            for(unsigned bit=0;bit<8;bit++){ra=(uint8_t)((ra<<1)|(a&1));rb=(uint8_t)((rb<<1)|(b&1));a>>=1;b>>=1;}
            u->framebuffer[i]=rb;u->framebuffer[FV_SCREEN_BYTES-1-i]=ra;
        }
    }
}

