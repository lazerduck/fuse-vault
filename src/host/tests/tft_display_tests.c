#include <assert.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "fuse_vault/tft_display.h"
static bool dc, cs=true, lit;
static unsigned cmd, writes, y, x0, x1;
static uint64_t now;
static uint8_t panel[80][320];
void gpio_init(unsigned p){(void)p;}
void gpio_set_dir(unsigned p,bool out){(void)p;(void)out;}
void gpio_set_function(unsigned p,unsigned f){(void)p;(void)f;}
void gpio_put(unsigned p,bool v){
    if(p==FUSE_VAULT_TFT_CHIP_SELECT_PIN)cs=v;
    if(p==FUSE_VAULT_TFT_DATA_COMMAND_PIN)dc=v;
    if(p==FUSE_VAULT_TFT_BACKLIGHT_PIN)lit=v==FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL;
}
void sleep_ms(unsigned ms){now+=ms*1000;}
uint64_t time_us_64(void){return now;}
unsigned spi_init(void *s,unsigned b){(void)s;assert(b==8000000);return b;}
void spi_set_format(void *s,unsigned b,unsigned p,unsigned h,unsigned o){(void)s;assert(b==8 && !p && !h && !o);}
int spi_write_blocking(void *s,const uint8_t *d,size_t n){
    (void)s;assert(!cs);
    if(!dc){assert(n==1);cmd=d[0];}
    else if(cmd==0x2a){assert(n==4);x0=d[1];x1=d[3];}
    else if(cmd==0x2b){assert(n==4 && d[1]==d[3]);y=d[1]-26;assert(y<80);}
    else if(cmd==0x2c){assert(n==320 && x0==1 && x1==160);memcpy(panel[y],d,n);writes++;}
    return (int)n;
}
int main(void){
    uint8_t fb[12800]={0};fb[0]=255;fb[7]=255;fb[12799]=255;
    fv_tft_init();assert(!lit && cs);
    for(unsigned i=0;i<80;i++){unsigned before=writes;fv_tft_poll(fb);assert(writes==before+1 && cs);if(i<79)assert(!lit);}
    assert(lit && writes==80);
    assert(panel[0][0]==255 && panel[0][1]==255 && panel[0][2]==0 && panel[0][14]==255);
    assert(panel[79][318]==255 && panel[79][319]==255);
    now+=10000;for(unsigned i=0;i<80;i++)fv_tft_poll(fb);assert(writes==80);
    /* A cancelled entry must clear pixels, including updates during a scan. */
    now+=10000;fb[0]=0;fv_tft_poll(fb);fb[12799]=0;
    for(unsigned i=1;i<80;i++)fv_tft_poll(fb);
    assert(writes==82 && panel[0][0]==0 && panel[79][318]==0);
    fv_tft_init();assert(!lit);fv_tft_poll(fb);assert(writes==83);
    /* Every RGB332 value, including changes that keep pixels nonzero. */
    fv_tft_init();
    for(unsigned i=0;i<256;i++)fb[i]=(uint8_t)i;
    for(unsigned i=0;i<80;i++)fv_tft_poll(fb);
    static const unsigned red[8]={0,4,9,13,18,22,27,31};
    static const unsigned blue[4]={0,10,21,31};
    for(unsigned i=0;i<256;i++) {
        unsigned expected=(red[i>>5]<<11)|(((i>>2)&7)*9<<5)|blue[i&3];
        unsigned x=(i%160)*2;
        assert(panel[i/160][x]==(expected>>8));
        assert(panel[i/160][x+1]==(expected&255));
    }
    /* Startup animates independently of the live UI, then hands back at 500 ms. */
    fv_tft_init();
    unsigned before = writes;
    fv_tft_show_startup();
    assert(lit && writes == before + 80);
    uint8_t initial[sizeof(panel)];
    memcpy(initial,panel,sizeof(panel));
    memset(fb,255,sizeof(fb));
    for(unsigned f=1;f<10;f++) {
        now+=50000;
        for(unsigned i=0;i<80;i++)fv_tft_poll(fb);
    }
    assert(memcmp(initial,panel,sizeof(panel)));
    assert(panel[0][0]==0); /* Still artwork, not the white live UI. */
    now+=49999;
    for(unsigned i=0;i<80;i++)fv_tft_poll(fb);
    assert(panel[0][0]==0);
    now+=10000; /* Next scheduled scan after the deadline. */
    for(unsigned i=0;i<80;i++)fv_tft_poll(fb);
    for(unsigned y=0;y<80;y++)
        for(unsigned x=0;x<320;x++)assert(panel[y][x]==255);
    return 0;
}
