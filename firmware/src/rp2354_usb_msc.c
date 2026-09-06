#include "fuse_vault/rp2354_usb_msc.h"

#include "tusb.h"

#include <stdint.h>
#include <string.h>

static fv_block_device_t *blocks;
static bool initialized;
static bool attached;
static bool eject_requested;
static bool storage_failure_requested;
static uint8_t sector[FV_BLOCK_SIZE];

static bool backend_ready(void) {
    return attached && blocks != NULL && blocks->ops != NULL &&
           blocks->ops->read != NULL && blocks->ops->write != NULL &&
           blocks->ops->sync != NULL && blocks->ops->block_count != NULL &&
           blocks->ops->is_present != NULL && blocks->ops->is_present(blocks);
}

static void medium_error(uint8_t command) {
    tud_msc_set_sense(0u, SCSI_SENSE_MEDIUM_ERROR, command, 0u);
}

bool fv_rp2354_usb_msc_init(void) {
    blocks = NULL;
    attached = false;
    eject_requested = false;
    storage_failure_requested = false;
    memset(sector, 0, sizeof(sector));
    return true;
}

bool fv_rp2354_usb_msc_attach(fv_block_device_t *plaintext_blocks) {
#if (!FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED || \
     !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED) && \
    !defined(FUSE_VAULT_COMPILE_UNVALIDATED_USB_MSC)
    (void)plaintext_blocks;
    return false;
#else
#if !FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED || \
    !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
    /* Compile-only coverage must never make an electrical routing claim. */
    if (plaintext_blocks != NULL) return false;
#endif
    if (attached || plaintext_blocks == NULL || plaintext_blocks->ops == NULL ||
        plaintext_blocks->ops->read == NULL ||
        plaintext_blocks->ops->write == NULL ||
        plaintext_blocks->ops->sync == NULL ||
        plaintext_blocks->ops->is_present == NULL ||
        !plaintext_blocks->ops->is_present(plaintext_blocks) ||
        plaintext_blocks->ops->block_count == NULL ||
        plaintext_blocks->ops->block_count(plaintext_blocks) == 0u) return false;
    blocks = plaintext_blocks;
    eject_requested = false;
    storage_failure_requested = false;
    if (!initialized) {
        if (!tusb_init()) {
            blocks = NULL;
            return false;
        }
        initialized = true;
    }
    attached = true;
    tud_connect();
    return true;
#endif
}

bool fv_rp2354_usb_msc_detach(void) {
    bool synced = true;
    attached = false;
    if (blocks != NULL && blocks->ops != NULL && blocks->ops->sync != NULL) {
        synced = blocks->ops->sync(blocks) == FV_BLOCK_OK;
    }
    if (initialized) tud_disconnect();
    blocks = NULL;
    memset(sector, 0, sizeof(sector));
    storage_failure_requested = false;
    return synced;
}

void fv_rp2354_usb_msc_task(void) {
    if (initialized) tud_task();
}

bool fv_rp2354_usb_msc_take_eject_request(void) {
    const bool requested = eject_requested;
    eject_requested = false;
    return requested;
}

bool fv_rp2354_usb_msc_take_storage_failure(void) {
    const bool requested = storage_failure_requested;
    storage_failure_requested = false;
    return requested;
}

uint8_t tud_msc_get_maxlun_cb(void) { return 0u; }

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "FUSEVLT ", 8u);
    memcpy(product_id, "Encrypted Vault ", 16u);
    memcpy(product_rev, "1.0 ", 4u);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    if (backend_ready()) return true;
    if (attached) storage_failure_requested = true;
    tud_msc_set_sense(0u, SCSI_SENSE_NOT_READY, 0x3au, 0x00u);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size) {
    (void)lun;
    const uint64_t count = backend_ready() ? blocks->ops->block_count(blocks) : 0u;
    *block_count = count <= UINT32_MAX ? (uint32_t)count : UINT32_MAX;
    *block_size = FV_BLOCK_SIZE;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return backend_ready();
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t buffer_size) {
    (void)lun;
    if (!backend_ready()) {
        if (attached) storage_failure_requested = true;
        medium_error(0x11u);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    if (buffer == NULL || offset > FV_BLOCK_SIZE ||
        buffer_size > FV_BLOCK_SIZE - offset ||
        (uint64_t)lba >= blocks->ops->block_count(blocks)) {
        medium_error(0x11u);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    if (blocks->ops->read(blocks, lba, 1u, sector) != FV_BLOCK_OK) {
        storage_failure_requested = true;
        medium_error(0x11u);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    memcpy(buffer, sector + offset, buffer_size);
    memset(sector, 0, sizeof(sector));
    return (int32_t)buffer_size;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t buffer_size) {
    (void)lun;
    if (!backend_ready()) {
        if (attached) storage_failure_requested = true;
        medium_error(0x0cu);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    if (buffer == NULL || offset > FV_BLOCK_SIZE ||
        buffer_size > FV_BLOCK_SIZE - offset ||
        (uint64_t)lba >= blocks->ops->block_count(blocks)) {
        medium_error(0x0cu);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    if ((offset != 0u || buffer_size != FV_BLOCK_SIZE) &&
        blocks->ops->read(blocks, lba, 1u, sector) != FV_BLOCK_OK) {
        storage_failure_requested = true;
        medium_error(0x0cu);
        return -1;
    }
    memcpy(sector + offset, buffer, buffer_size);
    if (blocks->ops->write(blocks, lba, 1u, sector) != FV_BLOCK_OK) {
        storage_failure_requested = true;
        medium_error(0x0cu);
        memset(sector, 0, sizeof(sector));
        return -1;
    }
    memset(sector, 0, sizeof(sector));
    return (int32_t)buffer_size;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject) {
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) {
        if (!backend_ready()) {
            if (attached) storage_failure_requested = true;
            medium_error(0x0cu);
            return false;
        }
        if (blocks->ops->sync(blocks) != FV_BLOCK_OK) {
            storage_failure_requested = true;
            medium_error(0x0cu);
            return false;
        }
        attached = false;
        eject_requested = true;
    }
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_command[16],
                        void *buffer, uint16_t buffer_size) {
    (void)lun;
    (void)buffer;
    (void)buffer_size;
    if (scsi_command == NULL) {
        tud_msc_set_sense(0u, SCSI_SENSE_ILLEGAL_REQUEST, 0x24u, 0x00u);
        return -1;
    }
    if (scsi_command[0] == 0x35u) { /* SYNCHRONIZE CACHE (10) */
        if (backend_ready() && blocks->ops->sync(blocks) == FV_BLOCK_OK) return 0;
        if (attached) storage_failure_requested = true;
        medium_error(0x0cu);
        return -1;
    }
    tud_msc_set_sense(0u, SCSI_SENSE_ILLEGAL_REQUEST, 0x20u, 0x00u);
    return -1;
}
