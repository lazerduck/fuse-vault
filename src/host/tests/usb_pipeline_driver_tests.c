/* Exercise the actual patched TinyUSB BOT state machine with delayed SD work. */
#define FV_REAL_MSC 1
#include "usb_storage_tests.c"
#include FV_MSC_DRIVER_FILE
static unsigned transfers,csws,retries;
static uint8_t ep;
static uint16_t length;
static bool stalled[256];
static dcd_event_t retry;
bool usbd_edpt_xfer(uint8_t port,uint8_t endpoint,uint8_t *buffer,uint16_t bytes){
    (void)port;CHECK(buffer==_mscd_epbuf.buf);++transfers;ep=endpoint;length=bytes;
    if(endpoint==0x81 && bytes==sizeof(msc_csw_t))++csws;
    return true;
}
void usbd_edpt_stall(uint8_t port,uint8_t endpoint){(void)port;stalled[endpoint]=true;}
void usbd_edpt_clear_stall(uint8_t port,uint8_t endpoint){(void)port;stalled[endpoint]=false;}
bool usbd_edpt_stalled(uint8_t port,uint8_t endpoint){(void)port;return stalled[endpoint];}
bool usbd_edpt_busy(uint8_t port,uint8_t endpoint){(void)port;(void)endpoint;return false;}
bool usbd_open_edpt_pair(uint8_t port,const uint8_t *desc,uint8_t count,uint8_t type,uint8_t *out,uint8_t *in){(void)port;(void)desc;(void)count;(void)type;*out=1;*in=0x81;return true;}
bool tud_control_status(uint8_t port,const tusb_control_request_t *request){(void)port;(void)request;return true;}
bool tud_control_xfer(uint8_t port,const tusb_control_request_t *request,void *buffer,uint16_t bytes){(void)port;(void)request;(void)buffer;(void)bytes;return true;}
void dcd_event_handler(const dcd_event_t *event,bool isr){(void)isr;retry=*event;++retries;}
static void again(void){CHECK(retries);--retries;CHECK(mscd_xfer_cb(0,retry.xfer_complete.ep_addr,XFER_RESULT_SUCCESS,retry.xfer_complete.len));}
static void begin(bool write,uint32_t bytes){
    fv_usb_storage_clear_transport();mscd_init();_mscd_itf.ep_out=1;_mscd_itf.ep_in=0x81;
    memset(stalled,0,sizeof(stalled));transfers=csws=retries=0;unlocked=true;failure=0;
    msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=42,.total_bytes=bytes,.dir=write?0:0x80,.cmd_len=10};
    cbw.command[0]=write?SCSI_CMD_WRITE_10:SCSI_CMD_READ_10;
    cbw.command[7]=(uint8_t)((bytes/512)>>8);cbw.command[8]=(uint8_t)(bytes/512);
    memcpy(_mscd_epbuf.buf,&cbw,sizeof(cbw));CHECK(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,sizeof(cbw)));
}
int main(void){
    begin(true,2*FV_USB_IO_BYTES);CHECK(ep==1 && length==FV_USB_IO_BYTES && !queued);
    memset(_mscd_epbuf.buf,0x51,FV_USB_IO_BYTES);
    CHECK(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,FV_USB_IO_BYTES));
    CHECK(queued && transfers==2 && !csws); /* Next USB receive armed with SD outstanding. */
    memset(_mscd_epbuf.buf,0xa2,FV_USB_IO_BYTES);
    CHECK(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,FV_USB_IO_BYTES));CHECK(retries && !csws);
    finish();again();CHECK(queued && !csws && retries); /* Final write still not acknowledged. */
    finish();again();CHECK(csws==1 && _mscd_itf.csw.status==MSC_CSW_STATUS_PASSED);
    CHECK(disk[0]==0x51 && disk[FV_USB_IO_BYTES]==0xa2);
    begin(false,2*FV_USB_IO_BYTES);CHECK(queued && !transfers);finish();again();
    CHECK(ep==0x81 && length==FV_USB_IO_BYTES && queued && _mscd_epbuf.buf[0]==0x51);
    finish();CHECK(_mscd_epbuf.buf[0]==0x51); /* Prefetch never overwrites USB-owned bytes. */
    CHECK(mscd_xfer_cb(0,0x81,XFER_RESULT_SUCCESS,FV_USB_IO_BYTES));CHECK(_mscd_epbuf.buf[0]==0xa2 && !csws);
    CHECK(mscd_xfer_cb(0,0x81,XFER_RESULT_SUCCESS,FV_USB_IO_BYTES));CHECK(csws==1);
    begin(true,512);failure=FV_BLOCK_ERROR_IO;memset(_mscd_epbuf.buf,0x55,512);
    CHECK(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,512));CHECK(!csws);finish();again();
    CHECK(_mscd_itf.csw.status==MSC_CSW_STATUS_FAILED);CHECK(!queued && zero(request.data,FV_USB_IO_BYTES));
    begin(true,2*FV_USB_IO_BYTES);memset(_mscd_epbuf.buf,0x71,FV_USB_IO_BYTES);
    CHECK(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,FV_USB_IO_BYTES));CHECK(queued);
    tusb_control_request_t reset={.bmRequestType=0x21,.bRequest=MSC_REQ_RESET};
    CHECK(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&reset));
    CHECK(!queued && !finished && zero(request.data,FV_USB_IO_BYTES));
    puts("Actual TinyUSB BOT overlap and durable-CSW tests passed");return 0;
}
