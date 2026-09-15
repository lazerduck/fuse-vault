/* Standalone, public test keys, no OTP/provisioning or vault access.
 * SD writes occur ONLY after the explicit ERASE-SD command. */
#include "fuse_vault/rp2354_sd.h"
#include "fuse_vault/rp2354_connector.h"
#include "fuse_vault/rp2354_usb_msc.h"
#include "fuse_vault/crypto_pipeline.h"
#include "crypto_aead.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#if !FUSE_VAULT_BASELINE_BENCH
#include "pico/bootrom.h"
#include "hardware/exception.h"
#else
#include "fuse_vault/baseline_bench.h"
#endif
#include "hardware/clocks.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

#if FUSE_VAULT_BASELINE_BENCH
static fv_rp2354_sd_t *bench_sd;
#define sd (*bench_sd)
static void (*bench_service)(void);
static bool requested, active;
#else
static fv_rp2354_sd_t sd;
static fv_connector_safety_t connector;
static fv_rp2354_connector_t connector_context;
#endif
static uint8_t ram_disk[128 * 1024];
static uint8_t buffer[16 * 1024] __attribute__((aligned(4)));
static uint8_t check[16 * 1024] __attribute__((aligned(4)));
static bool ram_attached;
static bool sd_attempted;
#if !FUSE_VAULT_BASELINE_BENCH
/* Survives the software return to ROM; contains only public startup state.
 * Read its ELF symbol address with picotool save after automatic fallback. */
volatile uint32_t bench_boot_trace[8]
    __attribute__((section(".uninitialized_data"), aligned(65536)));

static void boot_fallback(uint32_t reason) {
    bench_boot_trace[2] = reason;
    bench_boot_trace[3] = gpio_get_all();
    bench_boot_trace[4] = (uint32_t)time_us_64();
    /* ROM reboot is not a promise to reset our external mux's GPIOs. Release
     * SEL and OE# to the board's physical pull-downs (USB-C ROM default), after
     * disabling the mux while changing selection. Bench wiring is USB-C only. */
    fv_connector_safety_disable(&connector);
    gpio_set_dir(FUSE_VAULT_USB_SELECT_PIN, GPIO_IN);
    gpio_disable_pulls(FUSE_VAULT_USB_SELECT_PIN);
    gpio_set_dir(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN, GPIO_IN);
    gpio_disable_pulls(FUSE_VAULT_USB_OUTPUT_ENABLE_PIN);
    reset_usb_boot(0, 0);
}
static int64_t boot_timeout(alarm_id_t id, void *context) {
    (void)id; (void)context;
    if (bench_boot_trace[1] != 5) boot_fallback(1);
    return 0;
}
static void boot_hardfault(void) {
    bench_boot_trace[5] = *(volatile uint32_t *)UINT32_C(0xe000ed28); /* CFSR */
    bench_boot_trace[6] = *(volatile uint32_t *)UINT32_C(0xe000ed2c); /* HFSR */
    boot_fallback(2);
}
#endif

static void service(void) {
#if FUSE_VAULT_BASELINE_BENCH
    if (bench_service) bench_service();
#else
    if (!fv_connector_safety_poll(&connector)) return;
    fv_rp2354_usb_msc_task();
#endif
}
static void reply(const char *line) {
    size_t offset = 0, n = strlen(line);
    uint64_t deadline = time_us_64() + 2000000;
    while (offset < n && time_us_64() < deadline) {
        service();
        if (!tud_cdc_connected()) return;
        offset += tud_cdc_write(line + offset, (uint32_t)(n - offset));
        tud_cdc_write_flush();
    }
}
static void result(const char *name, unsigned batch, uint64_t bytes,
                   uint64_t us, bool ok) {
    char line[240];
    snprintf(line, sizeof(line),
        "{\"test\":\"%s\",\"batch_bytes\":%u,\"bytes\":%llu,\"us\":%llu,\"ok\":%s}\n",
        name, batch, (unsigned long long)bytes, (unsigned long long)us,
        ok ? "true" : "false");
    reply(line);
}
static void info(void) {
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1], line[320];
    pico_get_unique_board_id_string(serial, sizeof(serial));
    snprintf(line, sizeof(line),
        "{\"bench\":1,\"board\":\"%s\",\"cpu_hz\":%lu,\"sd_sectors\":%llu,"
        "\"sd_bus\":\"4-bit/25MHz\",\"ram_bytes\":%u,\"msc_buffer\":%u,"
        "\"build\":\"%s %s\"}\n", serial, (unsigned long)clock_get_hz(clk_sys),
        (unsigned long long)(sd_attempted && sd.interface.ops
            ? sd.interface.ops->block_count(&sd.interface) : 0),
        (unsigned)sizeof(ram_disk), CFG_TUD_MSC_EP_BUFSIZE, __DATE__, __TIME__);
    reply(line);
    reply(sd_attempted ? "{\"sd_initialization_attempted\":true}\n"
                       : "{\"sd_initialization_attempted\":false}\n");
#if FUSE_VAULT_BENCH_FIXED_USB_C
    uint32_t pins[20];
    fv_rp2354_connector_diagnostic_snapshot(pins);
    snprintf(line, sizeof(line),
        "{\"fixed_usb_c\":true,\"presence_samples\":%lu,\"presence_changes\":%lu,\"last_pattern\":%lu}\n",
        (unsigned long)pins[0], (unsigned long)pins[1], (unsigned long)pins[2]);
    reply(line);
    for (unsigned i = 0; i < 16; ++i) {
        if (!pins[4+i]) continue;
        snprintf(line, sizeof(line), "{\"presence_pattern\":%u,\"samples\":%lu}\n",
            i, (unsigned long)pins[4+i]);
        reply(line);
    }
#endif
}

static fv_block_result_t ram_read(fv_block_device_t *d, uint64_t first,
                                uint32_t count, uint8_t *out) {
    (void)d;
    if (!out || first >= 256 || !count || count > 256 - first)
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(out, ram_disk + first * 512, count * 512);
    return FV_BLOCK_OK;
}
static fv_block_result_t ram_write(fv_block_device_t *d, uint64_t first,
                                 uint32_t count, const uint8_t *in) {
    (void)d;
    if (!in || first >= 256 || !count || count > 256 - first)
        return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(ram_disk + first * 512, in, count * 512);
    return FV_BLOCK_OK;
}
static fv_block_result_t ram_sync(fv_block_device_t *d) {(void)d; return FV_BLOCK_OK;}
static uint64_t ram_count(const fv_block_device_t *d) {(void)d; return 256;}
static bool ram_present(const fv_block_device_t *d) {(void)d; return true;}
static const fv_block_device_ops_t ram_ops = {ram_read, ram_write, ram_sync, ram_count, ram_present};
static fv_block_device_t ram = {.ops = &ram_ops};

static void pattern(uint32_t first, unsigned count) {
    for (unsigned s = 0; s < count; ++s) {
        uint32_t state = (first+s) ^ UINT32_C(0xa5f03127);
        for (unsigned j = 0; j < 512; ++j) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            buffer[s * 512 + j] = (uint8_t)state;
        }
    }
}
static void sd_test(bool write) {
    /* Each pass uses the first 4 MiB. Write pass destroys partition/vault headers. */
    const unsigned batches[] = {1, 8, 32};
    if (!sd_attempted || !sd.interface.ops ||
        sd.interface.ops->block_count(&sd.interface) < 8192) {
        reply("{\"error\":\"SD missing or smaller than 4 MiB\"}\n"); return;
    }
    for (unsigned b = 0; b < 3; ++b) {
        unsigned n = batches[b];
        uint64_t elapsed = 0, bytes = 0;
        bool ok = true;
        for (uint32_t first = 0; first < 8192; first += n) {
            if (write) pattern(first, n);
            uint64_t start = time_us_64();
            fv_block_result_t r = write
                ? sd.interface.ops->write(&sd.interface, first, n, buffer)
                : sd.interface.ops->read(&sd.interface, first, n, buffer);
            elapsed += time_us_64() - start;
            if (r != FV_BLOCK_OK) {ok = false; break;}
            bytes += n * 512;
            service();
        }
        if (write && ok) {
            uint64_t start = time_us_64();
            ok = sd.interface.ops->sync(&sd.interface) == FV_BLOCK_OK;
            elapsed += time_us_64() - start;
        }
        result(write ? "sd_write" : "sd_read", n*512, bytes, elapsed, ok);
        if (!ok) return;
        /* Verification is separate and excluded from write timing. */
        if (write) {
            uint64_t start = time_us_64();
            bytes = 0;
            for (uint32_t first = 0; first < 8192; first += 32) {
                pattern(first, 32);
                if (sd.interface.ops->read(&sd.interface, first, 32, check) != FV_BLOCK_OK ||
                    memcmp(buffer, check, sizeof(buffer))) {ok = false; break;}
                bytes += sizeof(buffer);
                service();
            }
            result("sd_verify", n*512, bytes, time_us_64()-start, ok);
            if (!ok) return;
        }
    }
}
static void crypto_test(void) {
    uint8_t key[16] = {1}, nonce[16] = {0}, aad[88] = {0};
    uint8_t plain[512] = {0}, cipher[528], decoded[512];
    uint64_t enc = 0, dec = 0;
    bool ok = true;
    unsigned completed = 0;
    for (unsigned i = 0; i < 2048; ++i) {
        memcpy(nonce, &i, sizeof(i));
        unsigned long long length = 0;
        uint64_t t = time_us_64();
        int r = crypto_aead_encrypt(cipher, &length, plain, 512, aad, sizeof(aad), NULL, nonce, key);
        enc += time_us_64()-t;
        if (r || length != sizeof(cipher)) {ok = false; break;}
        t = time_us_64();
        r = crypto_aead_decrypt(decoded, &length, NULL, cipher, sizeof(cipher), aad, sizeof(aad), nonce, key);
        dec += time_us_64()-t;
        if (r || length != 512 || memcmp(plain, decoded, 512)) {ok = false; break;}
        ++completed;
        service();
    }
    result("ascon_encrypt_88byte_aad", 512, completed*512u, enc, ok);
    result("ascon_decrypt_88byte_aad", 512, completed*512u, dec, ok);
    fv_volume_master_key_t vmk = {.bytes = {1}};
    uint8_t vault[16] = {1}, epoch[16] = {2};
    for (size_t preset = 0; preset < fv_crypto_stack_preset_count(); ++preset) {
        fv_crypto_pipeline_t pipeline;
        fv_encryption_stack_descriptor_t stack;
        ok = fv_crypto_stack_preset(preset, &stack) &&
             fv_crypto_pipeline_init(&pipeline, &stack, &vmk, vault);
        enc = dec = completed = 0;
        for (unsigned i = 0; ok && i < 1024; ++i) {
            memset(decoded, 0, sizeof(decoded));
            uint64_t t = time_us_64();
            ok = fv_crypto_pipeline_encrypt_block(&pipeline, i, 1, epoch, i+1, decoded);
            enc += time_us_64()-t;
            if (!ok) break;
            t = time_us_64();
            ok = fv_crypto_pipeline_decrypt_block(&pipeline, i, 1, epoch, i+1, decoded);
            dec += time_us_64()-t;
            ok = ok && memcmp(plain, decoded, 512) == 0;
            if (ok) ++completed;
            service();
        }
        char name[64];
        snprintf(name, sizeof(name), "pipeline_preset_%u_encrypt", (unsigned)preset);
        result(name, 512, completed*512u, enc, ok);
        snprintf(name, sizeof(name), "pipeline_preset_%u_decrypt", (unsigned)preset);
        result(name, 512, completed*512u, dec, ok);
        fv_crypto_pipeline_clear(&pipeline);
    }
}
static void command(const char *cmd) {
#if FUSE_VAULT_BASELINE_BENCH
    if (!strcmp(cmd, "B")) {
        reply("{\"bench_mode\":true,\"startup\":\"existing_headless\"}\n{\"done\":true}\n");
        return;
    }
#endif
    if (ram_attached && strcmp(cmd, "INFO")) {
        reply("{\"error\":\"Power-cycle before another baseline; RAM disk is attached\"}\n{\"done\":true}\n");
        return;
    }
    if (!strcmp(cmd, "INFO")) info();
    else if (!strcmp(cmd, "INIT-SD")) {
        reply("{\"stage\":\"initializing_sd\"}\n");
        /* USB and INFO must work even if the card driver cannot initialize. */
        if (!sd_attempted) {
            sd_attempted = true;
            fv_rp2354_sd_init(&sd);
        }
        reply(sd.interface.ops && sd.interface.ops->is_present(&sd.interface)
            ? "{\"sd_ready\":true}\n" : "{\"error\":\"SD initialization failed\"}\n");
    }
    else if (!strcmp(cmd, "READ-SD")) sd_test(false);
    else if (!strcmp(cmd, "ERASE-SD")) sd_test(true);
    else if (!strcmp(cmd, "CRYPTO")) crypto_test();
    else if (!strcmp(cmd, "RAM-USB")) {
        ram_attached = fv_rp2354_usb_msc_attach(&ram);
        reply(ram_attached ? "{\"ram_attached\":true}\n" : "{\"error\":\"RAM attach failed\"}\n");
    } else reply("{\"error\":\"Unknown command\"}\n");
    reply("{\"done\":true}\n");
}
static void process_commands(void) {
    static char line[40];
    static unsigned used;
    static bool overflow;
    while (tud_cdc_available()) {
        char c; tud_cdc_read(&c, 1);
        if (c == '\n') {
            line[used] = 0;
            if (used && !overflow) command(line);
            else if (overflow) reply("{\"error\":\"Command too long\",\"done\":true}\n");
            used = 0; overflow = false;
        } else if (c != '\r') {
            if (used < sizeof(line)-1) line[used++] = c;
            else overflow = true;
        }
    }
}
#if FUSE_VAULT_BASELINE_BENCH
void fv_baseline_bench_request(void) { requested = true; }
bool fv_baseline_bench_poll(fv_rp2354_sd_t *raw, bool allowed,
                            void (*service_fn)(void)) {
    if (requested) {
        requested = false;
        bench_service = service_fn;
        if (!allowed) {
            reply("{\"error\":\"Lock the vault before entering bench mode\"}\n{\"done\":true}\n");
            return false;
        }
        bench_sd = raw;
        sd_attempted = true; /* Existing product startup already initialized it. */
        active = true;
        reply("{\"bench_mode\":true,\"startup\":\"existing_headless\"}\n{\"done\":true}\n");
    }
    if (!active) return false;
    service();
    if (fv_rp2354_usb_msc_take_eject_request()) {
        fv_rp2354_usb_msc_detach(); ram_attached = false;
    }
    process_commands();
    return true;
}
#else
int main(void) {
    for (unsigned i = 0; i < 8; ++i) bench_boot_trace[i] = 0;
    bench_boot_trace[0] = UINT32_C(0x46564254);
    bench_boot_trace[1] = 1;
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, boot_hardfault);
    add_alarm_in_ms(10000, boot_timeout, NULL, true);
    fv_rp2354_connector_init(&connector_context);
    bench_boot_trace[1] = 2;
    /* Presence GPIOs must agree and identify one connector. Retry with the mux
     * disabled while power/pins settle; never bypass conflict checks. */
    while (!fv_connector_safety_init(&connector, &fv_rp2354_connector_ops,
                                    &connector_context, NULL, NULL) ||
           !fv_connector_safety_route(&connector)) sleep_ms(10);
    bench_boot_trace[1] = 3;
    if (!fv_rp2354_usb_msc_init()) boot_fallback(3);
    bench_boot_trace[1] = 4;
    for (;;) {
        service();
        if (tud_mounted()) bench_boot_trace[1] = 5;
        if (fv_rp2354_usb_msc_take_eject_request()) {
            fv_rp2354_usb_msc_detach(); ram_attached = false;
        }
        process_commands();
    }
}
#endif
