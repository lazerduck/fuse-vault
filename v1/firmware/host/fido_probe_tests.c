#include "fuse_vault/fido_probe.h"
#include "fuse_vault/rp2354_usb_msc.h"
#include "tusb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t *, uint16_t);
bool tud_msc_test_unit_ready_cb(uint8_t);
static bool ready = true, accept = true;
static unsigned connects, disconnects, deinits, sends;
static uint8_t sent[64];
bool tusb_init(void) { return true; }
bool tud_deinit(uint8_t port) { assert(port == 0); ++deinits; return true; }
void tud_connect(void) { ++connects; }
void tud_disconnect(void) { ++disconnects; }
void tud_task(void) {}
bool tud_hid_ready(void) { return ready; }
bool tud_hid_report(uint8_t id, const void *r, uint16_t n) {
    assert(id == 0 && n == 64);
    if (!accept) return false;
    memcpy(sent, r, 64); ++sends; return true;
}
void tud_msc_set_sense(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    (void)a; (void)b; (void)c; (void)d;
}
static void frame(uint8_t r[64], uint32_t cid, uint8_t cmd, unsigned size) {
    memset(r, 0, 64);
    r[0] = (uint8_t)(cid >> 24); r[1] = (uint8_t)(cid >> 16);
    r[2] = (uint8_t)(cid >> 8); r[3] = (uint8_t)cid;
    r[4] = cmd; r[5] = (uint8_t)(size >> 8); r[6] = (uint8_t)size;
}
static uint32_t channel(fv_fido_probe_t *p) {
    uint8_t r[64], out[64];
    frame(r, UINT32_MAX, 0x86, 8);
    memcpy(r + 7, "nonce123", 8);
    fv_fido_probe_receive(p, r, 0);
    assert(fv_fido_probe_peek(p, out));
    assert(out[4] == 0x86 && out[6] == 17);
    assert(memcmp(out + 7, "nonce123", 8) == 0 && out[23] == 0x0c);
    uint32_t cid = (uint32_t)out[15] << 24 | (uint32_t)out[16] << 16 |
                   (uint32_t)out[17] << 8 | out[18];
    assert(cid != 0 && cid != UINT32_MAX);
    fv_fido_probe_sent(p); return cid;
}
static void expect_error(fv_fido_probe_t *p, uint8_t code) {
    uint8_t r[64]; assert(fv_fido_probe_peek(p, r));
    assert(r[4] == 0xbf && r[6] == 1 && r[7] == code);
    fv_fido_probe_sent(p);
}
static void protocol(void) {
    fv_fido_probe_t p;
    uint8_t r[64], out[64], second[64];
    fv_fido_probe_reset(&p);
    uint32_t cid = channel(&p), other = channel(&p);
    frame(r, cid, 0x90, 1); r[7] = 4;
    fv_fido_probe_receive(&p, r, 1);
    assert(fv_fido_probe_peek(&p, out));
    assert(fv_fido_probe_peek(&p, second) && memcmp(out, second, 64) == 0);
    /* Exact CBOR profile: no PIN, UV, extensions or algorithm claims. */
    static const uint8_t info[] = {
        0, 0xa4, 1, 0x81, 0x68, 'F','I','D','O','_','2','_','0',
        3, 0x50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        4, 0xa3, 0x62,'r','k',0xf4, 0x62,'u','p',0xf4,
        0x64,'p','l','a','t',0xf4, 5,0x19,4,0
    };
    assert(out[4] == 0x90 && out[6] == sizeof(info));
    assert(memcmp(out + 7, info, sizeof(info)) == 0);
    fv_fido_probe_sent(&p);
    for (unsigned cmd = 1; cmd <= 12; ++cmd) {
        if (cmd == 4) continue;
        frame(r, cid, 0x90, 1); r[7] = (uint8_t)cmd;
        fv_fido_probe_receive(&p, r, 2); assert(fv_fido_probe_peek(&p, out));
        assert(out[4] == 0x90 && out[6] == 1 && out[7] == 1);
        fv_fido_probe_sent(&p);
    }
    /* Full-size fragmented PING and byte-for-byte response reassembly. */
    uint8_t payload[FV_FIDO_MESSAGE_SIZE], echoed[FV_FIDO_MESSAGE_SIZE];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    frame(r, cid, 0x81, sizeof(payload)); memcpy(r + 7, payload, 57);
    fv_fido_probe_receive(&p, r, 10);
    frame(r, other, 0x81, 0); fv_fido_probe_receive(&p, r, 11); expect_error(&p, 6);
    size_t pos = 57; uint8_t seq = 0;
    while (pos < sizeof(payload)) {
        frame(r, cid, seq++, 0);
        size_t n = sizeof(payload) - pos; if (n > 59) n = 59;
        memcpy(r + 5, payload + pos, n); fv_fido_probe_receive(&p, r, 12); pos += n;
    }
    pos = 0; seq = 0;
    while (fv_fido_probe_peek(&p, out)) {
        unsigned offset = pos == 0 ? 7 : 5;
        if (pos != 0) assert(out[4] == seq++);
        size_t n = sizeof(echoed) - pos; if (n > 64 - offset) n = 64 - offset;
        memcpy(echoed + pos, out + offset, n); pos += n; fv_fido_probe_sent(&p);
    }
    assert(pos == sizeof(payload) && memcmp(payload, echoed, pos) == 0);
    frame(r, cid, 0x81, FV_FIDO_MESSAGE_SIZE + 1u); fv_fido_probe_receive(&p, r, 20); expect_error(&p, 3);
    frame(r, 0, 0x81, 0); fv_fido_probe_receive(&p, r, 20); expect_error(&p, 0x0b);
    frame(r, UINT32_MAX, 0x90, 1); fv_fido_probe_receive(&p, r, 20); expect_error(&p, 0x0b);
    frame(r, cid, 0x81, 60); fv_fido_probe_receive(&p, r, UINT32_MAX - 1000);
    fv_fido_probe_tick(&p, 1999); expect_error(&p, 5); /* Timer wrap. */
    frame(r, cid, 0x81, 60); fv_fido_probe_receive(&p, r, 21);
    frame(r, cid, 1, 0); fv_fido_probe_receive(&p, r, 22); expect_error(&p, 4);
    frame(r, cid, 0x81, 60); fv_fido_probe_receive(&p, r, 23);
    frame(r, cid, 0x91, 0); fv_fido_probe_receive(&p, r, 24);
    assert(!p.receiving && !p.transmitting);
    frame(r, cid, 0x81, 60); fv_fido_probe_receive(&p, r, 25);
    frame(r, cid, 0x86, 8); fv_fido_probe_receive(&p, r, 26);
    assert(!p.receiving && fv_fido_probe_peek(&p, out) && out[4] == 0x86);
    fv_fido_probe_reset(&p);
    frame(r, cid, 0x90, 1); fv_fido_probe_receive(&p, r, 30); expect_error(&p, 0x0b);
    assert(fv_pico_fido_get_info(out, 1) == 0);
}
static unsigned processing_action;
static size_t active_dispatch(void *context, uint32_t cid, const uint8_t *request,
    size_t size, uint8_t *response, size_t capacity) {
    fv_fido_probe_t *p = context;
    assert(size == 1 && request[0] == 4 && capacity >= 1 && p->processing);
    uint8_t incoming[64];
    frame(incoming, cid + 1u, 0x81, 0);
    fv_fido_probe_receive(p, incoming, 1);
    assert(p->control_pending && p->control_report[4] == 0xbf && p->control_report[7] == 6);
    p->control_pending = false;
    frame(incoming, cid + 1u, 0x91, 0);
    fv_fido_probe_receive(p, incoming, 1);
    assert(!p->cancelled);
    frame(incoming, cid, processing_action == 0 ? 0x91 : 0x86, processing_action == 0 ? 0 : 8);
    memcpy(incoming + 7, "resync12", 8);
    fv_fido_probe_receive(p, incoming, 1);
    assert(p->cancelled);
    response[0] = 0; return 1;
}
static void active_requests(void) {
    for (processing_action = 0; processing_action < 2; ++processing_action) {
        fv_fido_probe_t p;
        fv_fido_probe_reset(&p);
        uint32_t cid = channel(&p); (void)channel(&p);
        p.dispatch = active_dispatch; p.dispatch_context = &p;
        uint8_t incoming[64], out[64];
        frame(incoming, cid, 0x90, 1); incoming[7] = 4;
        fv_fido_probe_receive(&p, incoming, 1);
        assert(!p.processing && fv_fido_probe_peek(&p, out));
        if (processing_action == 0) assert(out[4] == 0x90 && out[7] == 0x2d);
        else assert(out[4] == 0x86 && memcmp(out + 7, "resync12", 8) == 0);
    }
}
static size_t usb_active_dispatch(void *context, uint32_t cid, const uint8_t *request,
    size_t size, uint8_t *response, size_t capacity) {
    (void)context;
    assert(size == 1 && request[0] == 4 && capacity > 0);
    assert(fv_rp2354_usb_fido_poll(200, 2));
    assert(sent[4] == 0xbb && sent[6] == 1 && sent[7] == 2);
    uint8_t report[64];
    frame(report, cid, 0x91, 0);
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,report,64);
    assert(fv_rp2354_usb_fido_cancelled());
    assert(!fv_rp2354_usb_fido_poll(201, 2));
    response[0] = 0; return 1;
}
static void usb_active(void) {
    assert(fv_rp2354_usb_msc_init());
    assert(fv_rp2354_usb_fido_attach_engine(usb_active_dispatch, NULL));
    uint8_t report[64];
    frame(report, UINT32_MAX, 0x86, 8);
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,report,64);
    fv_rp2354_usb_task_at(0);
    assert(sent[4] == 0x86);
    uint32_t cid = sent[18];
    frame(report, cid, 0x90, 1); report[7] = 4;
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,report,64);
    fv_rp2354_usb_task_at(1);
    assert(sent[4] == 0x90 && sent[7] == 0x2d);
    assert(fv_rp2354_usb_msc_detach());
}
static void usb(void) {
    uint8_t r[64];
    assert(fv_rp2354_usb_msc_init());
    assert(connects == 0 && !fv_rp2354_usb_is_fido());
    assert(fv_rp2354_usb_fido_attach());
    assert(connects == 1 && fv_rp2354_usb_is_fido());
    assert(!fv_rp2354_usb_fido_attach() && !tud_msc_test_unit_ready_cb(0));
    frame(r, UINT32_MAX, 0x86, 8); memcpy(r + 7, "nonce123", 8);
    ready = false;
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, r, 64);
    fv_rp2354_usb_task_at(1); assert(sends == 0);
    ready = true; accept = false;
    fv_rp2354_usb_task_at(2); assert(sends == 0);
    accept = true; fv_rp2354_usb_task_at(3);
    assert(sends == 1 && sent[4] == 0x86 && memcmp(sent + 7, "nonce123", 8) == 0);
    /* Queue overflow fails closed instead of overwriting report data. */
    for (unsigned i = 0; i < 9; ++i) tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, r, 64);
    fv_rp2354_usb_task_at(4);
    assert(!fv_rp2354_usb_is_fido() && disconnects == 1 && deinits == 1);
    assert(fv_rp2354_usb_msc_take_storage_failure());
    fv_rp2354_usb_task_at(5); assert(sends == 1);
    assert(fv_rp2354_usb_fido_attach());
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, r, 63);
    fv_rp2354_usb_task_at(6); assert(fv_rp2354_usb_msc_take_storage_failure());
    assert(fv_rp2354_usb_fido_attach());
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, r, 64);
    assert(fv_rp2354_usb_msc_detach());
    fv_rp2354_usb_task_at(7); assert(sends == 1);
}
static fv_block_result_t read_block(fv_block_device_t *d, uint64_t b, uint32_t n, uint8_t *o) {
    (void)d; (void)b; (void)n; (void)o; return FV_BLOCK_OK;
}
static fv_block_result_t write_block(fv_block_device_t *d, uint64_t b, uint32_t n, const uint8_t *o) {
    (void)d; (void)b; (void)n; (void)o; return FV_BLOCK_OK;
}
static fv_block_result_t sync_block(fv_block_device_t *d) { (void)d; return FV_BLOCK_OK; }
static uint64_t count_block(const fv_block_device_t *d) { (void)d; return 4; }
static bool present_block(const fv_block_device_t *d) { (void)d; return true; }
static void modes(void) {
    static const fv_block_device_ops_t ops = {
        .read = read_block, .write = write_block, .sync = sync_block,
        .block_count = count_block, .is_present = present_block,
    };
    fv_block_device_t blocks = {.ops = &ops};
    assert(fv_rp2354_usb_msc_attach(&blocks));
    assert(tud_msc_test_unit_ready_cb(0) && !fv_rp2354_usb_is_fido());
    assert(!fv_rp2354_usb_fido_attach());
    assert(fv_rp2354_usb_msc_detach());
    assert(fv_rp2354_usb_fido_attach());
    assert(!tud_msc_test_unit_ready_cb(0));
    assert(!fv_rp2354_usb_msc_attach(&blocks));
    assert(fv_rp2354_usb_msc_detach());
    assert(fv_rp2354_usb_msc_attach(&blocks));
    assert(tud_msc_test_unit_ready_cb(0));
    assert(fv_rp2354_usb_msc_detach());
}
static void malformed_streams(void) {
    fv_fido_probe_t p; uint8_t r[64], out[64]; uint32_t seed = 7;
    for (unsigned run = 0; run < 100; ++run) {
        fv_fido_probe_reset(&p); uint32_t cid = channel(&p);
        for (unsigned i = 0; i < 100; ++i) {
            for (unsigned j = 0; j < 64; ++j) {
                seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
                r[j] = (uint8_t)(seed >> 24);
            }
            /* Exercise both valid-channel parser paths and invalid channels. */
            if (i % 2 == 0) { r[0] = r[1] = r[2] = 0; r[3] = (uint8_t)cid; }
            fv_fido_probe_receive(&p, r, i * 100);
            fv_fido_probe_tick(&p, i * 100);
            while (fv_fido_probe_peek(&p, out)) fv_fido_probe_sent(&p);
        }
    }
    for (size_t capacity = 0; capacity < 64; ++capacity) {
        memset(out, 0xa5, sizeof(out));
        (void)fv_pico_fido_get_info(out, capacity);
        for (size_t i = capacity; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    }
}
int main(void) {
    protocol(); active_requests(); usb(); modes(); usb_active(); malformed_streams();
    puts("FIDO discovery and USB isolation tests passed");
}
