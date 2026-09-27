#include "pico/stdlib.h"
#include "tusb.h"
#include "device_ui.h"
#if FV_DEVICE_UI
#include "device_ui_adapter.h"
#endif
/* Board rev1: OE# low enables; SEL low=C, high=A. VBUS senses active high. */
void fv_usb_mux_poll(void){
    static unsigned route,candidate;static uint64_t stable_at,connect_at;
    uint64_t now=time_us_64();unsigned sensed=fv_usb_route(gpio_get(2),gpio_get(3));
    if(sensed!=candidate){candidate=sensed;stable_at=now;}
    if(candidate!=route && now-stable_at>=25000){
        tud_disconnect();gpio_put(1,1);route=candidate;connect_at=now+25000;
        gpio_put(0,route==2);
#if FV_USB_MSC
        extern void tud_umount_cb(void);tud_umount_cb();
#endif
#if FV_DEVICE_UI
        fv_device_ui_disconnect();
#endif
    }
    if(connect_at && now>=connect_at){connect_at=0;if(route){gpio_put(1,0);tud_connect();}}
}
