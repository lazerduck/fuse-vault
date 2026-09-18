#include "bringup.h"
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4022,.bcdDevice=0x0100,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1};
static const uint8_t configuration[]={
    TUD_CONFIG_DESCRIPTOR(1,2,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN,0,100),
    TUD_CDC_DESCRIPTOR(0,0,0x81,8,0x02,0x82,64)};
const uint8_t *tud_descriptor_device_cb(void){return (const uint8_t *)&device;}
const uint8_t *tud_descriptor_configuration_cb(uint8_t i){(void)i;return configuration;}
const uint16_t *tud_descriptor_string_cb(uint8_t index,uint16_t language) {
    (void)language; static uint16_t result[33]; char serial[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];
    const char *text;
    if(!index){result[0]=0x0304;result[1]=0x0409;return result;}
    if(index==1)text="Fuse Vault";
    else if(index==2)text="Fuse Vault SECURITY DEBUG";
    else if(index==3){pico_get_unique_board_id_string(serial,sizeof(serial));text=serial;}
    else return NULL;
    size_t n=strlen(text);if(n>32)n=32;
    for(size_t i=0;i<n;i++)result[i+1]=(uint8_t)text[i];
    result[0]=(uint16_t)(0x0300|((n+1)*2));return result;
}
/* One bounded ASCII request / JSON line response at a time. Core0 owns USB;
 * core1 owns all crypto, TRNG, SD and OTP work. No logs interleave replies. */
char reply[FV_REPLY_BYTES];
void security_usb_poll(void) {
    static fv_command command;
    static size_t used,sent,total;
    static bool busy,sending,discard,was_connected,poisoned;
    bool connected=tud_cdc_connected();
    if(!connected && was_connected) {
        used=0;sending=false;poisoned=false;discard=busy;
        tud_cdc_read_flush();tud_cdc_write_clear();
    }
    was_connected=connected;
    if(busy) {
        uint32_t n;
        if(!queue_try_remove(&responses,&n))return;
        busy=false;
        if(connected && !discard){total=n;sent=0;sending=true;}
        discard=false;
    }
    if(!connected || poisoned)return;
    if(sending) {
        sent+=tud_cdc_write(reply+sent,total-sent);tud_cdc_write_flush();
        if(sent==total)sending=false;
        return;
    }
    while(tud_cdc_available()) {
        char ch;
        if(tud_cdc_read(&ch,1)!=1)return;
        if(ch=='\n') {
            command.text[used]=0;
#if FV_DEBUG_STARTUP
            /* Handled on core0 without touching authority or worker-owned state.
             * No worker request is outstanding here (busy was checked above). */
            bool boot=!strcmp(command.text,"BOOT"),start=!strcmp(command.text,"START");
            if(boot || start || startup_stage!=14) {
                if(start)startup_requested=true;
                int n=snprintf(reply,FV_REPLY_BYTES,
                    "{\"command\":\"boot\",\"ok\":%s,\"stage\":%u,\"start_requested\":%s,"
                    "\"sense_a\":%u,\"sense_c\":%u,\"sense_a_duplicate\":%u,\"sense_c_duplicate\":%u}\n",
                    (boot||start)?"true":"false",(unsigned)startup_stage,startup_requested?"true":"false",
                    (unsigned)gpio_get(2),(unsigned)gpio_get(3),(unsigned)gpio_get(16),(unsigned)gpio_get(17));
                total=(size_t)n;sent=0;sending=true;used=0;return;
            }
#endif
            if(queue_try_add(&commands,&command)){busy=true;used=0;}
            return;
        }
        if(ch<' ' || ch>'~' || used>=sizeof(command.text)-1){poisoned=true;return;}
        command.text[used++]=ch;
    }
}
