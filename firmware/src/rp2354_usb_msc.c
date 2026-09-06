#include "fuse_vault/rp2354_usb_msc.h"

#include "tusb.h"

#include <stdint.h>
#include <string.h>
#if FUSE_VAULT_ENABLE_FIDO2
#include "fuse_vault/fido_probe.h"
static fv_fido_probe_t probe;
static uint8_t reports[8][64];
static uint32_t report_times[8], current_time, keepalive_time;
static unsigned report_head, report_count;
static bool report_overflow;
#endif

static fv_block_device_t *blocks;
static bool initialized;
static bool fido_mode;
static bool attached;
static bool eject_requested;
static bool storage_failure_requested;
static uint8_t sector[FV_BLOCK_SIZE];

bool fv_rp2354_usb_is_fido(void) { return fido_mode; }

bool fv_rp2354_usb_fido_attach(void) {
#if FUSE_VAULT_ENABLE_FIDO2 && FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED && FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
    if (initialized || attached || blocks != NULL) return false;
    fv_fido_probe_reset(&probe);
    report_head = report_count = 0; report_overflow = false;
    fido_mode = true;
    if (!tusb_init()) { fido_mode = false; return false; }
    initialized = true;
    tud_connect();
    return true;
#else
    return false;
#endif
}

bool fv_rp2354_usb_fido_attach_engine(fv_fido_dispatch_fn dispatch, void *context) {
#if FUSE_VAULT_ENABLE_FIDO2
    if (dispatch == NULL || !fv_rp2354_usb_fido_attach()) return false;
    probe.dispatch = dispatch; probe.dispatch_context = context;
    keepalive_time = 0;
    return true;
#else
    (void)dispatch; (void)context; return false;
#endif
}
bool fv_rp2354_usb_fido_cancelled(void) {
#if FUSE_VAULT_ENABLE_FIDO2
    return probe.cancelled || report_overflow || !initialized || !fido_mode;
#else
    return true;
#endif
}
bool fv_rp2354_usb_fido_poll(uint32_t now_ms, uint8_t status) {
#if FUSE_VAULT_ENABLE_FIDO2
    current_time = now_ms;
    if (!initialized || !fido_mode || !probe.processing) return false;
    tud_task();
    if (probe.control_pending && tud_hid_ready() &&
        tud_hid_report(0, probe.control_report, sizeof(probe.control_report))) probe.control_pending = false;
    if (probe.cancelled || report_overflow) return false;
    if ((uint32_t)(now_ms - keepalive_time) >= 100u && tud_hid_ready()) {
        uint32_t cid = probe.active_channel;
        uint8_t report[64] = { (uint8_t)(cid >> 24), (uint8_t)(cid >> 16),
            (uint8_t)(cid >> 8), (uint8_t)cid, 0xbb, 0, 1, status };
        if (tud_hid_report(0, report, sizeof(report))) keepalive_time = now_ms;
    }
    return true;
#else
    (void)now_ms; (void)status; return false;
#endif
}

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
    if (initialized || attached || plaintext_blocks == NULL || plaintext_blocks->ops == NULL ||
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
    fido_mode = false;
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
    if (initialized) {
        tud_disconnect();
        if (!tud_deinit(0)) synced = false;
        initialized = false;
    }
    fido_mode = false;
#if FUSE_VAULT_ENABLE_FIDO2
    fv_fido_probe_reset(&probe);
    memset(reports, 0, sizeof(reports));
    report_head = report_count = 0; report_overflow = false;
#endif
    blocks = NULL;
    memset(sector, 0, sizeof(sector));
    storage_failure_requested = false;
    return synced;
}

void fv_rp2354_usb_msc_task(void) {
    fv_rp2354_usb_task_at(0);
}

void fv_rp2354_usb_task_at(uint32_t now_ms) {
#if FUSE_VAULT_ENABLE_FIDO2
    current_time = now_ms;
#else
    (void)now_ms;
#endif
    if (!initialized) return;
    tud_task();
#if FUSE_VAULT_ENABLE_FIDO2
    if (!fido_mode) return;
    if (report_overflow) {
        (void)fv_rp2354_usb_msc_detach();
        storage_failure_requested = true; /* Existing runtime fail-closed path. */
        return;
    }
    /* Drain at most one request per tick; never overwrite an unsent response. */
    if (!probe.transmitting && report_count) {
        fv_fido_probe_tick(&probe, report_times[report_head]);
        if (!probe.transmitting) {
            uint8_t incoming[64];
            uint32_t received = report_times[report_head];
            memcpy(incoming, reports[report_head], sizeof(incoming));
            memset(reports[report_head], 0, 64);
            report_head = (report_head + 1) % 8; --report_count;
            fv_fido_probe_receive(&probe, incoming, received);
        }
    } else if (!report_count) fv_fido_probe_tick(&probe, now_ms);
    uint8_t report[64];
    if (probe.control_pending) {
        if (tud_hid_ready() && tud_hid_report(0, probe.control_report, sizeof(probe.control_report)))
            probe.control_pending = false;
        return;
    }
    if (tud_hid_ready() && fv_fido_probe_peek(&probe, report) &&
        tud_hid_report(0, report, sizeof(report))) fv_fido_probe_sent(&probe);
#endif
}

#if FUSE_VAULT_ENABLE_FIDO2
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                             hid_report_type_t report_type, uint8_t *buffer,
                             uint16_t requested) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)requested;
    return 0; /* FIDO uses interrupt reports, not feature/input GET_REPORT. */
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t type, const uint8_t *buffer,
                           uint16_t size) {
    if (!initialized || !fido_mode || instance != 0 || report_id != 0 ||
        type != HID_REPORT_TYPE_OUTPUT) return;
    if (size == 64 && probe.processing) {
        fv_fido_probe_receive(&probe, buffer, current_time);
        return;
    }
    if (size != 64 || report_count == 8) { report_overflow = true; return; }
    unsigned tail = (report_head + report_count) % 8;
    memcpy(reports[tail], buffer, 64); report_times[tail] = current_time;
    ++report_count;
}
#endif

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

/* A disconnected or sleeping host must not retain an unlocked FIDO session. */
void tud_umount_cb(void) {
    if (fido_mode) {
        eject_requested = true;
#if FUSE_VAULT_ENABLE_FIDO2
        probe.cancelled = true;
#endif
    }
}
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    tud_umount_cb();
}
