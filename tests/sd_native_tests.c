#include "fuse_vault/rp2354_sd.h"
#include "pico/stdlib.h"
#include "hw_config.h"
#include "hardware/dma.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>
static bool present=true,io_fail,status_fail,not_ready;
static unsigned aborts,read_calls,write_calls,sync_calls,last_count;
static uint64_t ticks;
fake_dma_t native_test_dma;
void gpio_init(unsigned p){(void)p;}
void gpio_set_dir(unsigned p,bool o){(void)p;(void)o;}
void gpio_disable_pulls(unsigned p){(void)p;}
bool gpio_get(unsigned p){(void)p;return present;}
absolute_time_t make_timeout_time_us(uint64_t d){return ticks+d;}
bool time_reached(absolute_time_t d){ticks+=10000;return ticks>=d;}
void pio_sm_set_enabled(void*p,uint n,bool e){(void)p;(void)n;(void)e;}
void dma_channel_set_irq1_enabled(uint n,bool e){(void)n;(void)e;}
void dma_channel_abort(uint n){(void)n;++aborts;}
static unsigned init(sd_card_t*c){c->state.sectors=4096;c->state.m_Status=0;c->sdio_if_p->state.ocr=1u<<30;c->sdio_if_p->state.resources_claimed=true;return 0;}
static void deinit(sd_card_t*c){c->state.m_Status=STA_NOINIT;}
static int read_sd(sd_card_t*c,uint8_t*b,uint32_t first,uint32_t n){(void)c;assert(first+n<=4096);++read_calls;last_count=n;memset(b,0xa5,n*512u);return io_fail?1:0;}
static int write_sd(sd_card_t*c,const uint8_t*b,uint32_t first,uint32_t n){(void)c;(void)b;assert(first+n<=4096);++write_calls;last_count=n;return io_fail?1:0;}
static int sync_sd(sd_card_t*c){(void)c;++sync_calls;return 0;}
bool sd_init_driver(void){sd_card_t*c=sd_get_by_num(0);assert(c->sdio_if_p->baud_rate==25000000);assert(c->sdio_if_p->D0_gpio==6 && c->sdio_if_p->CMD_gpio==5);c->init=init;c->deinit=deinit;c->read_blocks=read_sd;c->write_blocks=write_sd;c->sync=sync_sd;return true;}
int rp2040_sdio_command_R1(sd_card_t*c,uint8_t cmd,uint32_t arg,uint32_t*r){(void)c;(void)arg;assert(cmd==13);*r=status_fail?(1u<<19):not_ready?0:((1u<<8)|(4u<<9));return 0;}
int main(void){
 fv_rp2354_sd_t sd;static alignas(4) uint8_t b[257*512];
 assert(fv_rp2354_sd_init(&sd)&&sd.initialized);
 fv_block_device_t*d=&sd.interface;
 assert(d->ops->read(d,0,2,b)==FV_BLOCK_OK && read_calls==1 && last_count==2);
 assert(d->ops->write(d,0,2,b)==FV_BLOCK_OK && write_calls==1 && last_count==2 && sync_calls==1);
 assert(d->ops->read(d,0,257,b)==FV_BLOCK_OK && read_calls==3 && last_count==1);
 assert(d->ops->read(d,4095,2,b)==FV_BLOCK_ERROR_OUT_OF_RANGE);
 assert(d->ops->read(d,0,1,b+1)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
 assert(d->ops->write(d,0,0,b)==FV_BLOCK_ERROR_INVALID_ARGUMENT);
 io_fail=true;assert(d->ops->read(d,0,2,b)==FV_BLOCK_ERROR_IO);
 for(unsigned i=0;i<1024;i++)assert(b[i]==0);
 assert(!sd.initialized && aborts>=2 && fv_rp2354_sd_take_failure_event(&sd));
 io_fail=false;assert(fv_rp2354_sd_reinitialize(&sd));
 status_fail=true;assert(d->ops->write(d,0,2,b)==FV_BLOCK_ERROR_IO && !sd.initialized);
 status_fail=false;assert(fv_rp2354_sd_reinitialize(&sd));
 not_ready=true;assert(d->ops->sync(d)==FV_BLOCK_ERROR_IO && ticks>=1000000);
 not_ready=false;assert(fv_rp2354_sd_reinitialize(&sd));
 present=false;fv_rp2354_sd_poll(&sd);assert(!sd.initialized && fv_rp2354_sd_take_removal_event(&sd));
 present=true;fv_rp2354_sd_poll(&sd);assert(sd.initialized);
 fv_rp2354_sd_deinit(&sd);assert(!sd.initialized && !d->ops->is_present(d));
}
