#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
#define SCSI_SENSE_NOT_READY 2
#define SCSI_SENSE_MEDIUM_ERROR 3
#define SCSI_SENSE_ILLEGAL_REQUEST 5
#define SCSI_SENSE_UNIT_ATTENTION 6
bool tud_msc_set_sense(uint8_t,uint8_t,uint8_t,uint8_t);
void tud_msc_inquiry_cb(uint8_t,uint8_t[8],uint8_t[16],uint8_t[4]);
bool tud_msc_test_unit_ready_cb(uint8_t);
void tud_msc_capacity_cb(uint8_t,uint32_t*,uint16_t*);
bool tud_msc_is_writable_cb(uint8_t);
int32_t tud_msc_read10_cb(uint8_t,uint32_t,uint32_t,void*,uint32_t);
int32_t tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t*,uint32_t);
bool tud_msc_start_stop_cb(uint8_t,uint8_t,bool,bool);
int32_t tud_msc_scsi_cb(uint8_t,const uint8_t[16],void*,uint16_t);
#endif
