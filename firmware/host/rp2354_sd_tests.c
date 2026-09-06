#include "fuse_vault/rp2354_sd.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (step %zu)\n", \
    __FILE__, __LINE__, #x, cursor); exit(1); } } while (0)
typedef struct { uint8_t tx[512], rx[512]; size_t length; } exchange_t;
static exchange_t script[256];
static size_t count, cursor, fail_at;
static bool present, hardware_ok, idle_timeout;
static uint64_t now;
static uint32_t speeds[16];
static unsigned speed_count, releases;

void gpio_init(unsigned p) { (void)p; }
void gpio_set_dir(unsigned p, bool v) { (void)p; (void)v; }
void gpio_put(unsigned p, bool v) { (void)p; (void)v; }
bool gpio_get(unsigned p) { CHECK(p == FUSE_VAULT_SD_CARD_DETECT_PIN); return present; }
void gpio_pull_up(unsigned p) { (void)p; }
void gpio_disable_pulls(unsigned p) { (void)p; }
absolute_time_t make_timeout_time_us(uint64_t delay) { return now + delay; }
bool time_reached(absolute_time_t deadline) { now += 1000u; return now >= deadline; }
void sleep_ms(uint32_t delay) { now += (uint64_t)delay * 1000u; }

bool fv_rp2354_sd_spi_init(fv_rp2354_sd_spi_t *bus) {
    memset(bus, 0, sizeof(*bus)); bus->initialized = hardware_ok; return hardware_ok;
}
void fv_rp2354_sd_spi_deinit(fv_rp2354_sd_spi_t *bus) {
    bus->initialized = false; ++releases;
}
bool fv_rp2354_sd_spi_set_speed(fv_rp2354_sd_spi_t *bus, uint32_t hz) {
    CHECK(bus->initialized && !bus->failed); CHECK(speed_count < 16u);
    speeds[speed_count++] = hz; return true;
}
bool fv_rp2354_sd_spi_transfer(fv_rp2354_sd_spi_t *bus,
                              const uint8_t *tx, uint8_t *rx, size_t length) {
    CHECK(bus->initialized);
    if (bus->failed) return false;
    if (cursor == fail_at) {
        bus->failed = true; if (rx) memset(rx, 0, length); return false;
    }
    if (idle_timeout && cursor == count) {
        CHECK(length == 1u && (!tx || *tx == 0xffu));
        if (rx) *rx = 0xffu;
        return true;
    }
    CHECK(cursor < count);
    exchange_t *step = &script[cursor++];
    CHECK(length == step->length);
    for (size_t i = 0; i < length; ++i) CHECK((tx ? tx[i] : 0xffu) == step->tx[i]);
    if (rx) memcpy(rx, step->rx, length);
    return true;
}
static void reset(void) {
    count = cursor = speed_count = releases = 0u; fail_at = SIZE_MAX;
    now = 0; present = hardware_ok = true; idle_timeout = false;
}
static void bytes(const uint8_t *tx, const uint8_t *rx, size_t length) {
    CHECK(count < 256u && length <= 512u);
    exchange_t *step = &script[count++]; step->length = length;
    memset(step->tx, 0xff, length); memset(step->rx, 0xff, length);
    if (tx) memcpy(step->tx, tx, length);
    if (rx) memcpy(step->rx, rx, length);
}
static void byte(uint8_t tx, uint8_t rx) { bytes(&tx, &rx, 1u); }
static void clocks(void) { byte(0xffu, 0xffu); }
static uint8_t command_crc(const uint8_t *p) {
    unsigned crc = 0;
    for (unsigned i = 0; i < 5; ++i) {
        for (int b = 7; b >= 0; --b) {
            unsigned bit = ((p[i] >> b) & 1u) ^ ((crc >> 6u) & 1u);
            crc = ((crc << 1u) & 127u) ^ (bit ? 9u : 0u);
        }
    }
    return (uint8_t)(crc * 2u + 1u);
}
static uint16_t data_crc(const uint8_t *p, size_t n) {
    unsigned crc = 0;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (unsigned)p[i] << 8u;
        for (unsigned b = 0; b < 8; ++b)
            crc = ((crc << 1u) ^ ((crc & 0x8000u) ? 0x1021u : 0u)) & 65535u;
    }
    return (uint16_t)crc;
}
static void command(uint8_t cmd, uint32_t arg, uint8_t reply) {
    clocks(); clocks(); /* select and wait-ready */
    uint8_t p[6] = {(uint8_t)(0x40u | cmd), (uint8_t)(arg >> 24u),
        (uint8_t)(arg >> 16u), (uint8_t)(arg >> 8u), (uint8_t)arg, 0};
    p[5] = command_crc(p); bytes(p, NULL, sizeof(p)); byte(0xffu, reply);
}
static void read_data(const uint8_t *p, size_t n, bool corrupt) {
    byte(0xffu, 0xfeu); bytes(NULL, p, n);
    uint16_t crc = data_crc(p, n); if (corrupt) crc ^= 1u;
    byte(0xffu, (uint8_t)(crc >> 8u)); byte(0xffu, (uint8_t)crc); clocks();
}
static void initialize(bool high_capacity) {
    for (unsigned i = 0; i < 11; ++i) clocks();
    command(0u, 0u, 1u); clocks();
    command(8u, 0x1aau, 1u);
    byte(0xffu, 0u); byte(0xffu, 0u); byte(0xffu, 1u); byte(0xffu, 0xaau); clocks();
    command(55u, 0u, 1u); clocks(); command(41u, 0x40000000u, 0u); clocks();
    command(58u, 0u, 0u); byte(0xffu, high_capacity ? 0xc0u : 0x80u);
    for (unsigned i = 0; i < 3; ++i) byte(0xffu, 0u);
    clocks();
    if (!high_capacity) { command(16u, 512u, 0u); clocks(); }
    command(59u, 1u, 0u); clocks();
    uint8_t csd[16] = {0};
    if (high_capacity) { csd[0] = 0x40u; csd[9] = 3u; }
    else { csd[5] = 9u; csd[7] = 0xffu; csd[8] = 0xc0u; }
    command(9u, 0u, 0u); read_data(csd, sizeof(csd), false);
}
static void test_rw(bool high_capacity) {
    reset(); initialize(high_capacity); fv_rp2354_sd_t sd;
    CHECK(fv_rp2354_sd_init(&sd)); CHECK(cursor == count && sd.initialized);
    CHECK(speeds[0] == 400000u && speeds[1] == 8000000u);
    CHECK(sd.interface.ops->block_count(&sd.interface) == 4096u);
    uint8_t data[512], output[512];
    for (unsigned i = 0; i < 512; ++i) data[i] = (uint8_t)(i * 37u);
    uint32_t address = high_capacity ? 7u : 7u * 512u;
    command(17u, address, 0u); read_data(data, sizeof(data), false);
    CHECK(sd.interface.ops->read(&sd.interface, 7u, 1u, output) == FV_BLOCK_OK);
    CHECK(memcmp(data, output, sizeof(data)) == 0 && cursor == count);
    command(24u, address, 0u); clocks(); byte(0xfeu, 0xffu);
    bytes(data, NULL, sizeof(data)); uint16_t crc = data_crc(data, sizeof(data));
    byte((uint8_t)(crc >> 8u), 0xffu); byte((uint8_t)crc, 0xffu);
    byte(0xffu, 5u); byte(0xffu, 0u); clocks(); clocks();
    command(13u, 0u, 0u); byte(0xffu, 0u); clocks();
    CHECK(sd.interface.ops->write(&sd.interface, 7u, 1u, data) == FV_BLOCK_OK);
    CHECK(cursor == count);
    clocks(); clocks(); clocks();
    CHECK(sd.interface.ops->sync(&sd.interface) == FV_BLOCK_OK);
    command(17u, address, 0u); read_data(data, sizeof(data), true);
    CHECK(sd.interface.ops->read(&sd.interface, 7u, 1u, output) == FV_BLOCK_ERROR_INTEGRITY);
    CHECK(!sd.initialized && fv_rp2354_sd_take_failure_event(&sd));
    uint8_t zero[512] = {0}; CHECK(memcmp(output, zero, sizeof(output)) == 0);
    clocks(); fv_rp2354_sd_deinit(&sd); CHECK(releases == 1u && cursor == count);
}
static void test_dma_failure_and_reinsert(void) {
    reset(); initialize(true); fv_rp2354_sd_t sd; CHECK(fv_rp2354_sd_init(&sd));
    command(17u, 0u, 0u); byte(0xffu, 0xfeu); fail_at = count;
    uint8_t output[512];
    CHECK(sd.interface.ops->read(&sd.interface, 0u, 1u, output) == FV_BLOCK_ERROR_IO);
    CHECK(!sd.initialized && fv_rp2354_sd_take_failure_event(&sd));
    present = false; fv_rp2354_sd_poll(&sd);
    present = true; fail_at = SIZE_MAX; initialize(true); fv_rp2354_sd_poll(&sd);
    CHECK(sd.initialized && fv_rp2354_sd_take_insertion_event(&sd));
    CHECK(releases == 1u && speeds[2] == FV_SD_SPI_INIT_HZ);
    present = false; clocks(); fv_rp2354_sd_poll(&sd);
    CHECK(!sd.initialized && fv_rp2354_sd_take_removal_event(&sd));
    clocks(); fv_rp2354_sd_deinit(&sd); CHECK(cursor == count);
}
static void test_absence_and_timeout(void) {
    reset(); present = false; fv_rp2354_sd_t sd;
    CHECK(fv_rp2354_sd_init(&sd)); CHECK(!sd.initialized && cursor == 0u);
    present = true; initialize(true); fv_rp2354_sd_poll(&sd);
    CHECK(sd.initialized && fv_rp2354_sd_take_insertion_event(&sd));
    command(17u, 0u, 0u); idle_timeout = true; uint8_t output[512];
    CHECK(sd.interface.ops->read(&sd.interface, 0u, 1u, output) == FV_BLOCK_ERROR_IO);
    CHECK(now >= 250000u && !sd.initialized);
    fv_rp2354_sd_deinit(&sd);
    reset(); hardware_ok = false; CHECK(!fv_rp2354_sd_init(&sd));
}
int main(void) {
    test_rw(true); test_rw(false); test_dma_failure_and_reinsert(); test_absence_and_timeout();
    puts("SD SPI protocol, bulk transfers, CRC and failure tests passed."); return 0;
}
