#ifndef FUSE_VAULT_FIDO_PROBE_H
#define FUSE_VAULT_FIDO_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_FIDO_MESSAGE_SIZE 4096u
typedef size_t (*fv_fido_dispatch_fn)(void *context, uint32_t channel,
    const uint8_t *request, size_t size, uint8_t *response, size_t capacity);
typedef struct {
    uint32_t channels[4], next_channel, active_channel, rx_time, tx_channel;
    uint16_t rx_size, rx_used, tx_size, tx_used;
    uint8_t rx_command, rx_sequence, tx_command, tx_sequence;
    bool receiving, transmitting, processing, cancelled, resync_requested;
    bool control_pending;
    uint8_t resync_nonce[8], control_report[64];
    fv_fido_dispatch_fn dispatch;
    void *dispatch_context;
    uint8_t rx[FV_FIDO_MESSAGE_SIZE], tx[FV_FIDO_MESSAGE_SIZE];
} fv_fido_probe_t;

void fv_fido_probe_reset(fv_fido_probe_t *probe);
/* Caller applies backpressure while a response is pending. Reports are exactly
 * 64 bytes. A bound dispatcher owns credential state and durable commits;
 * the fallback GetInfo-only path is retained for isolated transport tests. */
void fv_fido_probe_receive(fv_fido_probe_t *probe, const uint8_t report[64], uint32_t now);
void fv_fido_probe_tick(fv_fido_probe_t *probe, uint32_t now);
bool fv_fido_probe_peek(const fv_fido_probe_t *probe, uint8_t report[64]);
void fv_fido_probe_sent(fv_fido_probe_t *probe);
size_t fv_pico_fido_get_info(uint8_t *output, size_t capacity);
#endif
