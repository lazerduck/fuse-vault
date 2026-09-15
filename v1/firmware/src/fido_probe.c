#include "fuse_vault/fido_probe.h"
#include <string.h>

enum { PING = 0x81, INIT = 0x86, CBOR = 0x90, CANCEL = 0x91, ERROR = 0xbf };
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static void put32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}
void fv_fido_probe_reset(fv_fido_probe_t *p) {
    memset(p, 0, sizeof(*p)); p->next_channel = 1;
}
static void reply(fv_fido_probe_t *p, uint32_t cid, uint8_t cmd, size_t size) {
    p->tx_channel = cid; p->tx_command = cmd; p->tx_size = (uint16_t)size;
    p->tx_used = 0; p->tx_sequence = 0; p->transmitting = true;
}
static void error(fv_fido_probe_t *p, uint32_t cid, uint8_t code) {
    p->tx[0] = code; reply(p, cid, ERROR, 1);
}
static bool known(const fv_fido_probe_t *p, uint32_t cid) {
    if (cid == 0 || cid == UINT32_MAX) return false;
    for (unsigned i = 0; i < 4; ++i) if (p->channels[i] == cid) return true;
    return false;
}
static void complete(fv_fido_probe_t *p) {
    p->receiving = false;
    if (p->rx_command == PING) {
        memcpy(p->tx, p->rx, p->rx_size);
        reply(p, p->active_channel, PING, p->rx_size);
    } else if (p->rx_command == CBOR) {
        if (p->dispatch != NULL) {
            p->processing = true; p->cancelled = false; p->resync_requested = false;
            size_t count = p->dispatch(p->dispatch_context, p->active_channel,
                p->rx, p->rx_size, p->tx, sizeof(p->tx));
            p->processing = false;
            if (p->resync_requested) {
                memcpy(p->tx, p->resync_nonce, 8); put32(p->tx + 8, p->active_channel);
                p->tx[12] = 2; p->tx[13] = 0; p->tx[14] = 1; p->tx[15] = 0; p->tx[16] = 0x0c;
                reply(p, p->active_channel, INIT, 17);
                memset(p->rx, 0, sizeof(p->rx)); return;
            }
            if (p->cancelled) { p->tx[0] = 0x2d; count = 1; }
            if (count == 0 || count > sizeof(p->tx)) { p->tx[0] = 0x7f; count = 1; }
            reply(p, p->active_channel, CBOR, count);
            memset(p->rx, 0, sizeof(p->rx));
            return;
        }
        size_t size = 0;
        p->tx[0] = 0x01; /* CTAP1_ERR_INVALID_COMMAND: no credential operations. */
        if (p->rx_size == 1 && p->rx[0] == 0x04) {
            size = fv_pico_fido_get_info(p->tx + 1, sizeof(p->tx) - 1);
            p->tx[0] = size ? 0 : 0x7f;
        } else if (p->rx_size > 1 && p->rx[0] == 0x04) {
            p->tx[0] = 0x03; /* Invalid length. */
        }
        reply(p, p->active_channel, CBOR, size + 1);
    }
    memset(p->rx, 0, sizeof(p->rx));
}
void fv_fido_probe_receive(fv_fido_probe_t *p, const uint8_t r[64], uint32_t now) {
    uint32_t cid = get32(r);
    if (p->processing) {
        if (r[4] == CANCEL && r[5] == 0 && r[6] == 0) {
            if (cid == p->active_channel) p->cancelled = true;
            return;
        }
        if (cid == p->active_channel && r[4] == INIT && r[5] == 0 && r[6] == 8) {
            p->cancelled = true; p->resync_requested = true;
            memcpy(p->resync_nonce, r + 7, 8); return;
        }
        if ((r[4] & 0x80u) && !p->control_pending) {
            memset(p->control_report, 0, sizeof(p->control_report));
            put32(p->control_report, cid); p->control_report[4] = ERROR;
            p->control_report[6] = 1;
            p->control_report[7] = (known(p, cid) || (cid == UINT32_MAX && r[4] == INIT)) ? 6 : 0x0b;
            p->control_pending = true;
        }
        return;
    }
    if (p->transmitting) return;
    if (cid == 0 || (cid == UINT32_MAX && r[4] != INIT)) {
        error(p, cid, 0x0b); return;
    }
    if (cid != UINT32_MAX && !known(p, cid)) { error(p, cid, 0x0b); return; }
    if (r[4] == INIT) {
        if (r[5] != 0 || r[6] != 8) { error(p, cid, 3); return; }
        if (p->receiving && cid != p->active_channel) { error(p, cid, 6); return; }
        uint32_t assigned = cid;
        if (cid == UINT32_MAX) {
            unsigned slot;
            for (slot = 0; slot < 4 && p->channels[slot] != 0; ++slot) {}
            if (slot == 4) { error(p, cid, 6); return; }
            assigned = p->next_channel++;
            p->channels[slot] = assigned;
        }
        p->receiving = false;
        memset(p->rx, 0, sizeof(p->rx));
        memcpy(p->tx, r + 7, 8); put32(p->tx + 8, assigned);
        p->tx[12] = 2; p->tx[13] = 0; p->tx[14] = 1; p->tx[15] = 0;
        p->tx[16] = 0x0c; /* CBOR, no CTAP1_MSG. */
        reply(p, cid, INIT, 17); return;
    }
    if (r[4] == CANCEL) {
        if (r[5] != 0 || r[6] != 0) { error(p, cid, 3); return; }
        if (cid == p->active_channel) {
            p->receiving = false; memset(p->rx, 0, sizeof(p->rx));
        }
        return;
    }
    if (p->receiving && cid != p->active_channel) { error(p, cid, 6); return; }
    size_t offset, count;
    if (r[4] & 0x80u) {
        if (p->receiving) { error(p, cid, 6); return; }
        uint16_t size = (uint16_t)((uint16_t)r[5] << 8 | r[6]);
        if (size > sizeof(p->rx) || (r[4] == CBOR && size == 0)) {
            error(p, cid, 3); return;
        }
        if (r[4] != PING && r[4] != CBOR) { error(p, cid, 1); return; }
        p->active_channel = cid; p->rx_command = r[4]; p->rx_size = size;
        p->rx_used = 0; p->rx_sequence = 0; p->receiving = true;
        offset = 7;
    } else {
        if (!p->receiving) return; /* Ignore an unsolicited continuation. */
        if (r[4] != p->rx_sequence++) {
            p->receiving = false; memset(p->rx, 0, sizeof(p->rx));
            error(p, cid, 4); return;
        }
        offset = 5;
    }
    count = (size_t)(p->rx_size - p->rx_used);
    if (count > 64 - offset) count = 64 - offset;
    memcpy(p->rx + p->rx_used, r + offset, count);
    p->rx_used = (uint16_t)(p->rx_used + count); p->rx_time = now;
    if (p->rx_used == p->rx_size) complete(p);
}
void fv_fido_probe_tick(fv_fido_probe_t *p, uint32_t now) {
    if (p->receiving && !p->transmitting && (uint32_t)(now - p->rx_time) >= 3000) {
        p->receiving = false; memset(p->rx, 0, sizeof(p->rx));
        error(p, p->active_channel, 5);
    }
}
bool fv_fido_probe_peek(const fv_fido_probe_t *p, uint8_t r[64]) {
    if (!p->transmitting) return false;
    memset(r, 0, 64); put32(r, p->tx_channel);
    size_t offset;
    if (p->tx_used == 0) {
        r[4] = p->tx_command; r[5] = (uint8_t)(p->tx_size >> 8);
        r[6] = (uint8_t)p->tx_size; offset = 7;
    } else { r[4] = p->tx_sequence; offset = 5; }
    size_t count = (size_t)(p->tx_size - p->tx_used);
    if (count > 64 - offset) count = 64 - offset;
    memcpy(r + offset, p->tx + p->tx_used, count); return true;
}
void fv_fido_probe_sent(fv_fido_probe_t *p) {
    if (!p->transmitting) return;
    size_t count = p->tx_used == 0 ? 57 : 59;
    if (p->tx_used != 0) ++p->tx_sequence;
    if (count >= (size_t)(p->tx_size - p->tx_used)) {
        p->transmitting = false; memset(p->tx, 0, sizeof(p->tx));
    } else p->tx_used = (uint16_t)(p->tx_used + count);
}
