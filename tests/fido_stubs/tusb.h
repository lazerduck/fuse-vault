#ifndef FV_FIDO_TEST_TUSB_H
#define FV_FIDO_TEST_TUSB_H
#include <stdint.h>
#include <stdbool.h>
typedef enum {HID_REPORT_TYPE_INPUT=1,HID_REPORT_TYPE_OUTPUT=2} hid_report_type_t;
bool tud_hid_ready(void);
bool tud_hid_report(uint8_t,const void *,uint16_t);
#endif
