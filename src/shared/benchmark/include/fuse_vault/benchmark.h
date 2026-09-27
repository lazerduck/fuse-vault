#ifndef FV_BENCHMARK_H
#define FV_BENCHMARK_H
#include "fuse_vault/crypto.h"
#include "fuse_vault/auth_store.h"
#include "fuse_vault/block_device.h"
#define FV_BENCH_MAGIC UINT32_C(0x33425646)
#define FV_BENCH_VERSION 5u
#define FV_BENCH_REQUEST_BYTES 32u
#define FV_BENCH_RESPONSE_BYTES 104u
#define FV_BENCH_BUFFER_BYTES 32768u
#define FV_BENCH_SCRATCH_BLOCKS 8192u
#define FV_BENCH_WRITE_TOKEN UINT32_C(0x45524153)
typedef enum { FV_BENCH_INFO=1, FV_BENCH_CONFIG=2, FV_BENCH_WRITE=3,
               FV_BENCH_READ=4, FV_BENCH_END=5, FV_BENCH_AUTH_CONFIG=6 } fv_bench_op;
typedef enum { FV_BENCH_OK=0, FV_BENCH_INVALID=1, FV_BENCH_NOT_READY=2,
               FV_BENCH_IO=3, FV_BENCH_CRYPTO=4, FV_BENCH_INTEGRITY=5 } fv_bench_status;
typedef struct {
    uint32_t op, sequence, lba, blocks, payload_bytes, algorithms;
} fv_bench_request;
typedef struct {
    uint32_t op, sequence, status, payload_bytes;
    uint64_t crypto_us, sd_us, setup_us, capacity_blocks;
    uint32_t cpu_hz, sd_hz;
    uint64_t hmac_us,metadata_us,metadata_reads,metadata_writes;
    uint32_t hmac_backend,hmac_self_test;
} fv_bench_response;
/* Wire encoding is explicit little-endian; C struct layout is never sent. */
bool fv_bench_decode(const uint8_t header[FV_BENCH_REQUEST_BYTES], fv_bench_request *);
void fv_bench_encode(const fv_bench_response *, uint8_t header[FV_BENCH_RESPONSE_BYTES]);
/* Platform supplies a ready block-device object, an opener and monotonic timer.
 * Engine and payload buffer have one owner at a time; no allocation or USB API. */
typedef struct {
    fv_block_device_t *device;
    bool (*open)(void *);
    void *open_context;
    uint64_t (*now_us)(void);
    uint32_t cpu_hz, sd_hz, configured_blocks;
    bool configured;
    bool hmac_checked,hmac_ok;
    fv_pipeline pipeline;
    bool authenticated;
    fv_auth_store store;
} fv_bench_engine;
void fv_bench_execute(fv_bench_engine *, const fv_bench_request *,
                      uint8_t buffer[FV_BENCH_BUFFER_BYTES], fv_bench_response *);
void fv_bench_reset(fv_bench_engine *);
#endif
