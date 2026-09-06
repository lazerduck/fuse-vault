#ifndef FUSE_VAULT_HOST_TUSB_H
#define FUSE_VAULT_HOST_TUSB_H

#include <stdbool.h>
#include <stdint.h>

#define SCSI_SENSE_NOT_READY 0x02u
#define SCSI_SENSE_MEDIUM_ERROR 0x03u
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05u

bool tusb_init(void);
bool tud_deinit(uint8_t rhport);
typedef enum { HID_REPORT_TYPE_INPUT = 1, HID_REPORT_TYPE_OUTPUT = 2,
               HID_REPORT_TYPE_FEATURE = 3 } hid_report_type_t;
bool tud_hid_ready(void);
bool tud_hid_report(uint8_t report_id, const void *report, uint16_t len);
void tud_connect(void);
void tud_disconnect(void);
void tud_task(void);
void tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code,
                       uint8_t add_sense_qualifier);

#endif
