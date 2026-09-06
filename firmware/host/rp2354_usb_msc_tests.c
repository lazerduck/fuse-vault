#include "fuse_vault/rp2354_usb_msc.h"
#include "tusb.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TinyUSB invokes these callbacks; declare them here because the production
 * adapter intentionally exposes only its application-facing API. */
uint8_t tud_msc_get_maxlun_cb(void);
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]);
bool tud_msc_test_unit_ready_cb(uint8_t lun);
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size);
bool tud_msc_is_writable_cb(uint8_t lun);
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t buffer_size);
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t buffer_size);
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject);
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_command[16],
                        void *buffer, uint16_t buffer_size);

#define TEST_BLOCKS 4u

typedef struct {
    uint8_t media[TEST_BLOCKS][FV_BLOCK_SIZE];
    bool present;
    fv_block_result_t read_result;
    fv_block_result_t write_result;
    fv_block_result_t sync_result;
    unsigned sync_calls;
} fake_blocks_t;

static unsigned init_calls;
static unsigned connect_calls;
static unsigned disconnect_calls;
static unsigned task_calls;
static unsigned sense_calls;
static uint8_t last_sense_key;

bool tusb_init(void) {
    ++init_calls;
    return true;
}

void tud_connect(void) { ++connect_calls; }
void tud_disconnect(void) { ++disconnect_calls; }
void tud_task(void) { ++task_calls; }

void tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code,
                       uint8_t add_sense_qualifier) {
    (void)lun;
    (void)add_sense_code;
    (void)add_sense_qualifier;
    ++sense_calls;
    last_sense_key = sense_key;
}

static fv_block_result_t fake_read(fv_block_device_t *device,
                                   uint64_t first_block,
                                   uint32_t block_count,
                                   uint8_t *output) {
    fake_blocks_t *fake = device->context;
    if (fake->read_result != FV_BLOCK_OK) return fake->read_result;
    if (block_count != 1u || first_block >= TEST_BLOCKS) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    memcpy(output, fake->media[first_block], FV_BLOCK_SIZE);
    return FV_BLOCK_OK;
}

static fv_block_result_t fake_write(fv_block_device_t *device,
                                    uint64_t first_block,
                                    uint32_t block_count,
                                    const uint8_t *input) {
    fake_blocks_t *fake = device->context;
    if (fake->write_result != FV_BLOCK_OK) return fake->write_result;
    if (block_count != 1u || first_block >= TEST_BLOCKS) {
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    }
    memcpy(fake->media[first_block], input, FV_BLOCK_SIZE);
    return FV_BLOCK_OK;
}

static fv_block_result_t fake_sync(fv_block_device_t *device) {
    fake_blocks_t *fake = device->context;
    ++fake->sync_calls;
    return fake->sync_result;
}

static uint64_t fake_block_count(const fv_block_device_t *device) {
    (void)device;
    return TEST_BLOCKS;
}

static bool fake_is_present(const fv_block_device_t *device) {
    const fake_blocks_t *fake = device->context;
    return fake->present;
}

static const fv_block_device_ops_t fake_ops = {
    .read = fake_read,
    .write = fake_write,
    .sync = fake_sync,
    .block_count = fake_block_count,
    .is_present = fake_is_present,
};

static void reset_stubs(void) {
    init_calls = 0u;
    connect_calls = 0u;
    disconnect_calls = 0u;
    task_calls = 0u;
    sense_calls = 0u;
    last_sense_key = 0u;
}

static fv_block_device_t make_device(fake_blocks_t *fake) {
    memset(fake, 0, sizeof(*fake));
    fake->present = true;
    fake->read_result = FV_BLOCK_OK;
    fake->write_result = FV_BLOCK_OK;
    fake->sync_result = FV_BLOCK_OK;
    return (fv_block_device_t){.ops = &fake_ops, .context = fake};
}

static void test_normal_io_and_eject(void) {
    fake_blocks_t fake;
    fv_block_device_t device = make_device(&fake);
    uint8_t read_buffer[23];
    uint8_t write_buffer[19];
    uint32_t block_count = 0u;
    uint16_t block_size = 0u;
    uint8_t vendor[8];
    uint8_t product[16];
    uint8_t revision[4];
    uint8_t sync_command[16] = {0x35u};

    for (size_t i = 0u; i < FV_BLOCK_SIZE; ++i) {
        fake.media[1][i] = (uint8_t)i;
    }
    memset(write_buffer, 0xa6, sizeof(write_buffer));
    reset_stubs();
    assert(fv_rp2354_usb_msc_init());
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(fv_rp2354_usb_msc_attach(&device));
    assert(init_calls == 1u);
    assert(connect_calls == 1u);
    assert(tud_msc_get_maxlun_cb() == 0u);
    tud_msc_inquiry_cb(0u, vendor, product, revision);
    assert(memcmp(vendor, "FUSEVLT ", sizeof(vendor)) == 0);
    assert(memcmp(product, "Encrypted Vault ", sizeof(product)) == 0);
    assert(memcmp(revision, "1.0 ", sizeof(revision)) == 0);
    assert(tud_msc_test_unit_ready_cb(0u));
    assert(tud_msc_is_writable_cb(0u));
    tud_msc_capacity_cb(0u, &block_count, &block_size);
    assert(block_count == TEST_BLOCKS);
    assert(block_size == FV_BLOCK_SIZE);
    assert(tud_msc_read10_cb(0u, 1u, 7u, read_buffer,
                             sizeof(read_buffer)) ==
           (int32_t)sizeof(read_buffer));
    assert(memcmp(read_buffer, fake.media[1] + 7u, sizeof(read_buffer)) == 0);
    assert(tud_msc_write10_cb(0u, 1u, 11u, write_buffer,
                              sizeof(write_buffer)) ==
           (int32_t)sizeof(write_buffer));
    assert(memcmp(fake.media[1] + 11u, write_buffer,
                  sizeof(write_buffer)) == 0);

    fv_rp2354_usb_msc_task();
    assert(task_calls == 1u);
    assert(tud_msc_start_stop_cb(0u, 0u, false, true));
    assert(fake.sync_calls == 1u);
    assert(fv_rp2354_usb_msc_take_eject_request());
    assert(!fv_rp2354_usb_msc_take_eject_request());
    assert(!tud_msc_test_unit_ready_cb(0u));
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(tud_msc_scsi_cb(0u, sync_command, NULL, 0u) == -1);
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(fv_rp2354_usb_msc_detach());
    assert(disconnect_calls == 1u);
}

static void test_backend_failures_request_lock(void) {
    fake_blocks_t fake;
    fv_block_device_t device = make_device(&fake);
    uint8_t buffer[FV_BLOCK_SIZE] = {0};

    reset_stubs();
    assert(fv_rp2354_usb_msc_init());
    assert(fv_rp2354_usb_msc_attach(&device));
    fake.read_result = FV_BLOCK_ERROR_INTEGRITY;
    assert(tud_msc_read10_cb(0u, 0u, 0u, buffer, sizeof(buffer)) == -1);
    assert(fv_rp2354_usb_msc_take_storage_failure());
    assert(!fv_rp2354_usb_msc_take_storage_failure());

    fake.read_result = FV_BLOCK_OK;
    fake.write_result = FV_BLOCK_ERROR_IO;
    assert(tud_msc_write10_cb(0u, 0u, 0u, buffer, sizeof(buffer)) == -1);
    assert(fv_rp2354_usb_msc_take_storage_failure());

    fake.write_result = FV_BLOCK_OK;
    fake.present = false;
    assert(!tud_msc_test_unit_ready_cb(0u));
    assert(fv_rp2354_usb_msc_take_storage_failure());
    assert(tud_msc_read10_cb(0u, 0u, 0u, buffer, sizeof(buffer)) == -1);
    assert(fv_rp2354_usb_msc_take_storage_failure());
    assert(fv_rp2354_usb_msc_detach());
}

static void test_protocol_errors_do_not_fake_media_failure(void) {
    fake_blocks_t fake;
    fv_block_device_t device = make_device(&fake);
    uint8_t buffer[2] = {0};
    uint8_t command[16] = {0xffu};

    reset_stubs();
    assert(fv_rp2354_usb_msc_init());
    assert(fv_rp2354_usb_msc_attach(&device));
    assert(tud_msc_read10_cb(0u, 0u, FV_BLOCK_SIZE, buffer,
                             sizeof(buffer)) == -1);
    assert(last_sense_key == SCSI_SENSE_MEDIUM_ERROR);
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(tud_msc_read10_cb(0u, TEST_BLOCKS, 0u, buffer,
                             sizeof(buffer)) == -1);
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(tud_msc_scsi_cb(0u, NULL, NULL, 0u) == -1);
    assert(last_sense_key == SCSI_SENSE_ILLEGAL_REQUEST);
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(tud_msc_scsi_cb(0u, command, NULL, 0u) == -1);
    assert(last_sense_key == SCSI_SENSE_ILLEGAL_REQUEST);
    assert(!fv_rp2354_usb_msc_take_storage_failure());
    assert(fv_rp2354_usb_msc_detach());
}

int main(void) {
    test_normal_io_and_eject();
    test_backend_failures_request_lock();
    test_protocol_errors_do_not_fake_media_failure();
    puts("rp2354 usb msc tests passed");
    return 0;
}
