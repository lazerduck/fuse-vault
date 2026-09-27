#include "bringup.h"
#include "tusb.h"
#if FV_USB_FIDO
#include "fido_adapter.h"
#include "fuse_vault/fido_hid_descriptor.h"
#endif
#include "pico/unique_id.h"
#include "pico/stdlib.h"
#if FV_DEBUG_BOOT_TRACE
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#endif
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
#endif
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4022,.bcdDevice=FV_USB_FIDO?0x0300:FV_USB_MSC?0x0200:0x0100,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1};
static const uint8_t configuration[]={
#if FV_USB_FIDO
    TUD_CONFIG_DESCRIPTOR(1,4,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_MSC_DESC_LEN+TUD_HID_INOUT_DESC_LEN,0,100),
    TUD_CDC_DESCRIPTOR(0,0,0x81,8,0x02,0x82,64),
    TUD_MSC_DESCRIPTOR(2,0,0x03,0x83,64),
    TUD_HID_INOUT_DESCRIPTOR(3,0,HID_ITF_PROTOCOL_NONE,sizeof(fv_fido_hid_report_descriptor),0x04,0x84,64,5)
#elif FV_USB_MSC
    TUD_CONFIG_DESCRIPTOR(1,3,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_MSC_DESC_LEN,0,100),
    TUD_CDC_DESCRIPTOR(0,0,0x81,8,0x02,0x82,64),
    TUD_MSC_DESCRIPTOR(2,0,0x03,0x83,64)
#else
    TUD_CONFIG_DESCRIPTOR(1,2,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN,0,100),
    TUD_CDC_DESCRIPTOR(0,0,0x81,8,0x02,0x82,64)
#endif
};
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
#if FV_DEVICE_UI
        if(!strncmp(command.text,"UNLOCK ",7) || !strcmp(command.text,"LOCK"))fv_device_ui_refresh();
#endif
#if FV_USB_MSC
        if(!strcmp(command.text,"LOCK"))fv_usb_storage_clear_transport();
#endif
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
#if FV_DEVICE_UI
            if(fv_device_ui_command(command.text,reply,FV_REPLY_BYTES)){
                total=strlen(reply);sent=0;sending=true;used=0;return;
            }
            if(atomic_load(&fv_ui_maintenance)
#if FV_USB_FIDO
                || fv_fido_busy()
#endif
            ){
                total=(size_t)snprintf(reply,FV_REPLY_BYTES,"{\"command\":\"error\",\"ok\":false,\"error\":\"device UI busy\"}\n");
                sent=0;sending=true;used=0;return;
            }
#endif
#if FV_DEBUG_STARTUP
            /* Handled on core0 without touching authority or worker-owned state.
             * No worker request is outstanding here (busy was checked above). */
            bool boot=!strcmp(command.text,"BOOT"),start=!strcmp(command.text,"START");
#if FV_DEBUG_BOOT_TRACE
            /* No worker or authority operation may be in progress on this path. */
            if(!strcmp(command.text,"REBOOT") && !startup_stage && !startup_requested) {
                int r=rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL,100,(uint32_t)-2,0);
                total=(size_t)snprintf(reply,FV_REPLY_BYTES,"{\"command\":\"boot\",\"ok\":%s,\"reboot_result\":%d}\n",r?"false":"true",r);
                sent=0;sending=true;used=0;return;
            }
#endif
            if(boot || start || startup_stage!=14) {
                if(start)startup_requested=true;
                int n=snprintf(reply,FV_REPLY_BYTES,
                    "{\"command\":\"boot\",\"ok\":%s,\"stage\":%u,\"start_requested\":%s,"
                    "\"sense_a\":%u,\"sense_c\":%u,\"sense_a_duplicate\":%u,\"sense_c_duplicate\":%u}\n",
                    (boot||start)?"true":"false",(unsigned)startup_stage,startup_requested?"true":"false",
                    (unsigned)gpio_get(2),(unsigned)gpio_get(3),(unsigned)gpio_get(16),(unsigned)gpio_get(17));
#if FV_DEBUG_BOOT_TRACE
                extern uint32_t boot_trace_previous[4];
                n-=2; /* Replace closing brace/newline with trace fields. */
                n+=snprintf(reply+n,FV_REPLY_BYTES-(size_t)n,
                    ",\"previous_trace_valid\":%s,\"previous_trace_stage\":%u}\n",
                    boot_trace_previous[0]==0x46564254?"true":"false",(unsigned)boot_trace_previous[1]);
#endif
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
