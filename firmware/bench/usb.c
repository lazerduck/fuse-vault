#include "bench.h"
#include "tusb.h"
#include "pico/unique_id.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4021,.bcdDevice=0x0500,
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
    else if(index==2)text="Fuse Vault V2 PIPELINE";
    else if(index==3){pico_get_unique_board_id_string(serial,sizeof(serial));text=serial;}
    else return NULL;
    size_t n=strlen(text);if(n>32)n=32;
    for(size_t i=0;i<n;i++)result[i+1]=(uint8_t)text[i];
    result[0]=(uint16_t)(0x0300|((n+1)*2));return result;
}
/* Binary framing over CDC bulk endpoints. Exactly one request owns the shared
 * buffer until its response has been copied into the USB transmit FIFO. */
uint8_t bench_buffer[FV_BENCH_BUFFER_BYTES] __attribute__((aligned(4)));
void bench_usb_poll(void) {
    static uint8_t header[FV_BENCH_REQUEST_BYTES], reply[FV_BENCH_RESPONSE_BYTES];
    static fv_bench_request request;
    static size_t header_used,payload_used,header_sent,payload_sent,payload_to_send;
    static bool busy,resetting,reset_needed=true,transmitting,poisoned,was_connected;
    bool connected=tud_cdc_connected();
    if(!connected && was_connected) {
        reset_needed=true;
        transmitting=false;
        header_used=payload_used=0;
        poisoned=false;
        tud_cdc_read_flush();
        tud_cdc_write_clear();
    }
    was_connected=connected;
    if(busy) {
        fv_bench_response response;
        if(!queue_try_remove(&responses,&response)) return;
        busy=false;
        if(resetting) {
            resetting=false;
            reset_needed=!connected;
        } else if(connected && !reset_needed) {
            fv_bench_encode(&response,reply);
            header_sent=payload_sent=0;
            payload_to_send=response.payload_bytes;
            transmitting=true;
        }
    }
    if(!connected) return;
    if(reset_needed) {
        fv_bench_request reset={0};
        if(queue_try_add(&commands,&reset)) {busy=true;resetting=true;}
        return;
    }
    if(poisoned) return; /* Reconnect after bad framing; never scan payload for magic. */
    if(transmitting) {
        if(header_sent<FV_BENCH_RESPONSE_BYTES)
            header_sent+=tud_cdc_write(reply+header_sent,FV_BENCH_RESPONSE_BYTES-header_sent);
        if(header_sent==FV_BENCH_RESPONSE_BYTES && payload_sent<payload_to_send)
            payload_sent+=tud_cdc_write(bench_buffer+payload_sent,payload_to_send-payload_sent);
        tud_cdc_write_flush();
        if(header_sent==FV_BENCH_RESPONSE_BYTES && payload_sent==payload_to_send) transmitting=false;
        return;
    }
    if(header_used<FV_BENCH_REQUEST_BYTES) {
        header_used+=tud_cdc_read(header+header_used,FV_BENCH_REQUEST_BYTES-header_used);
        if(header_used<FV_BENCH_REQUEST_BYTES) return;
        if(!fv_bench_decode(header,&request)) {poisoned=true;return;}
    }
    if(payload_used<request.payload_bytes) {
        payload_used+=tud_cdc_read(bench_buffer+payload_used,request.payload_bytes-payload_used);
        if(payload_used<request.payload_bytes) return;
    }
    if(queue_try_add(&commands,&request)) {
        busy=true;
        header_used=payload_used=0;
    }
}
