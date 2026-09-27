#ifndef FUSE_VAULT_FIDO_HID_H
#define FUSE_VAULT_FIDO_HID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_FIDO_MESSAGE_SIZE 4096u
typedef struct {
    uint32_t channels[4], next_channel, active_channel, rx_time, tx_channel;
    uint16_t rx_size, rx_used, tx_size, tx_used;
    uint8_t rx_command, rx_sequence, tx_command, tx_sequence;
    bool receiving, transmitting, processing, cancelled, resync_requested;
    bool control_pending;
    uint8_t resync_nonce[8], control_report[64];
    bool pending;
    uint8_t rx[FV_FIDO_MESSAGE_SIZE], tx[FV_FIDO_MESSAGE_SIZE];
} fv_fido_hid_t;

void fv_fido_hid_reset(fv_fido_hid_t *probe);
/* Core-0 owned, nonblocking. Exactly 64-byte reports. When pending becomes true,
 * hand immutable rx to the worker, clear pending, and leave processing set until
 * complete. Do not reset/reuse rx while a worker owns it; cancel and drain first.
 * control_pending is a separate busy/error report; it cannot clobber worker data. */
void fv_fido_hid_receive(fv_fido_hid_t *probe, const uint8_t report[64], uint32_t now);
void fv_fido_hid_tick(fv_fido_hid_t *probe, uint32_t now);
bool fv_fido_hid_peek(const fv_fido_hid_t *probe, uint8_t report[64]);
void fv_fido_hid_sent(fv_fido_hid_t *probe);
void fv_fido_hid_complete(fv_fido_hid_t *,const uint8_t *response,size_t size);
#endif
