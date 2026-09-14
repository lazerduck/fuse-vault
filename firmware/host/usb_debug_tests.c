#include "fuse_vault/usb_debug_display.h"
#include "fuse_vault/ui.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static bool connected;
static int request;
static unsigned capacity, used, clears;
static uint8_t wire[32 + 160 * 80 * 2];
static uint16_t color;
void gpio_init(unsigned p) { (void)p; }
void gpio_set_dir(unsigned p, bool output) {
    assert(!output || p == FUSE_VAULT_TFT_BACKLIGHT_PIN);
}
void gpio_put(unsigned p, bool value) {
    assert(p == FUSE_VAULT_TFT_BACKLIGHT_PIN);
    assert(value == !FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL);
}
void gpio_disable_pulls(unsigned p) { (void)p; }
absolute_time_t get_absolute_time(void) { return 1234; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)t; }
void fv_ui_draw_view(const fv_ui_view_t *v, fv_framebuffer_t *f) {
    (void)v;
    for (unsigned y=0;y<80;y++) for(unsigned x=0;x<160;x++) f->pixels[y][x]=color;
}
bool tud_cdc_connected(void) { return connected; }
uint32_t tud_cdc_available(void) { return request != 0; }
int32_t tud_cdc_read_char(void) { int c=request;request=0;return c; }
uint32_t tud_cdc_write_available(void) { return capacity; }
uint32_t tud_cdc_write(const void *b, uint32_t n) {
    /* Deliberately accept fewer bytes than advertised. */
    if(n>7)n=7;
    assert(used+n<=sizeof(wire));memcpy(wire+used,b,n);used+=n;return n;
}
uint32_t tud_cdc_write_flush(void) { return 0; }
bool tud_cdc_write_clear(void) { ++clears;return true; }
static uint32_t word(unsigned i) { uint32_t v;memcpy(&v,wire+4*i,4);return v; }
int main(void) {
    fv_ui_view_t view={0};
    assert(fv_usb_debug_display_ops.initialize(NULL));
    color=0xf800;
    assert(fv_usb_debug_display_ops.present(NULL,&view));
    fv_usb_debug_task(2,0);assert(clears==1 && used==0);
    connected=true;request='f';capacity=0;
    fv_usb_debug_set_boot(true,true,false,true,true,true,true,2);
    fv_usb_debug_task(2,5);assert(used==0);
    /* Changing the live image must not tear a snapshot under backpressure. */
    color=0x07e0;assert(fv_usb_debug_display_ops.present(NULL,&view));
    capacity=31;
    for(unsigned i=0;i<5000 && used<sizeof(wire);i++)fv_usb_debug_task(3,0);
    assert(used==sizeof(wire) && !memcmp(wire,"FVD1",4));
    assert(word(1)==25600 && word(2)==1 && word(3)==2 && word(4)==5);
    assert(word(5)==123 && word(6)==2 && word(7)==1234);
    for(unsigned i=32;i<sizeof(wire);i+=2)assert(wire[i]==0 && wire[i+1]==0xf8);
    /* Drop a partial transfer; reconnect returns a fresh coherent frame. */
    used=0;request='f';fv_usb_debug_task(3,0);assert(used==7);
    connected=false;fv_usb_debug_task(3,0);used=0;
    connected=true;request='f';
    for(unsigned i=0;i<5000 && used<sizeof(wire);i++)fv_usb_debug_task(3,0);
    assert(used==sizeof(wire) && word(2)==2);
    for(unsigned i=32;i<sizeof(wire);i+=2)assert(wire[i]==0xe0 && wire[i+1]==7);
    puts("USB framebuffer backpressure, snapshot and disconnect checks passed");
}
