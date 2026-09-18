# Laptop-controlled pipeline benchmark

The laptop sends test bytes over USB. The RP2354 encrypts each 512-byte sector,
writes the ciphertext to SD, and acknowledges the batch after the SD write
completes. For reads it fetches ciphertext, decrypts it, and returns the plaintext
for laptop verification. No disk is mounted and no RAM disk is exposed.

This is an already-unlocked test session using public test keys. Configuration
stays in RAM. There is no password handling, VMK wrapping, OTP programming or persistent
vault header yet. Optional `--integrity` enables HMAC-SHA-256 and packed metadata;
omitting it retains the cipher-only baseline. Additional stages can be added
to the same engine and timed independently. This is protocol 5 firmware, with Pico SHA acceleration enabled by default.

## What can be varied without reflashing

- Stack: `raw`, `aes`, `camellia`, `aes,camellia`, `camellia,aes`, or any ordered
  combination of up to four supported layers, including repeated algorithms
  with independent test keys per position.
- Batch: any whole-sector size from 512 bytes through 32768 bytes.
- Total test size: any whole-sector size up to 4 MiB; final batches may be short.
- Test seed and optional per-batch logging.

The crypto unit is always 512 bytes regardless of the USB/SD batch size. `raw`
is the same USB/SD path with encryption disabled for comparison (HMAC remains active if `--integrity` is set). The SD clock
is a build setting, initially 25 MHz; it is reported as a requested frequency,
not a measured pin clock. SD remains native four-bit PIO/DMA with multi-block
operations and CRC/status checking.

## Source structure

| Path | Responsibility |
|---|---|
| `src/crypto/` | Prepared ciphers and ordered pipelines |
| `src/storage/include/fuse_vault/block_device.h` | Raw block-device interface |
| `src/platform/rp2354/sd_native.c` | Reused four-bit SD adapter |
| `src/benchmark/engine.c` | Configurable block processing and stage timings |
| `src/benchmark/protocol.c` | Fixed, explicitly encoded binary headers |
| `firmware/bench/worker.c` | Core-1 platform adapter |
| `firmware/bench/usb.c` | Core-0 USB CDC framing and buffer ownership |
| `tools/board_bench.py` | Linux laptop runner, verification and JSON logs |

V1's native SD dependency was copied with its licenses to `third_party/pico_sd`.
Its recorded upstream revision is `d5e453404cdbfaa55ab30d285b6ab0b730e84a05`;
the copy includes the actual archived snapshot and any V1 adaptations, not an
assumption that an upstream checkout is identical. No V2 source target depends
on V1 source. SD pin mapping is CLK=4, CMD=5, DAT0..3=6..9. PIO1 and DMA IRQ1 are
owned by the SD worker. Card-detect polarity remains unconfirmed; the default
build relies on initialization/I/O errors for missing or removed cards.

## Build and first flash

```sh
cmake -S firmware/bench -B build-pico \
  -DPICO_SDK_PATH=/home/adam/pico-sdk \
  -DCMAKE_BUILD_TYPE=Release -DPICO_NO_PICOTOOL=1
cmake --build build-pico -j4
picotool uf2 convert build-pico/fuse_vault_v2_bench.elf \
  build-pico/fuse_vault_v2_bench.uf2 --family rp2350-arm-s --platform rp2350
```

The offline build avoids downloading picotool. The UF2 already in `build-pico`
was produced using the installed executable available under
`v1/firmware/build/_deps/picotool/picotool`; that is a build-tool reuse only.
An installed picotool package can also provide automatic UF2 generation through
the SDK if `PICO_NO_PICOTOOL` is omitted.

Load the UF2 through BOOTSEL. Use **USB-C only**, leave USB-A empty, and leave the
damaged display disconnected. Firmware holds the backlight off and uses fixed
USB-C routing, disabling the mux if either USB-A presence pin asserts. This is
bench routing, not the final dual-connector policy.

Completely remove power after flashing if the SD card previously ran in SPI
mode. This firmware enumerates as **cafe:4021**, product `Fuse Vault V2 PIPELINE`.
It has USB CDC only. Old cafe:4020 RAM-disk firmware is not accepted by this runner.
Flashing is manual; the script never flashes hardware. Nothing writes SD at boot.

## First check: no SD access

Identify the `/dev/serial/by-id/...` path, then:

```sh
python3 tools/board_bench.py --port /dev/serial/by-id/DEVICE \
  --info-only --output results/v2-info.json
```

This checks USB protocol/version, CPU clock and configuration without opening SD.
The script uses the Python standard library and needs permission to open that
serial port. Use one serial client at a time. No block-device permissions or
host filesystem formatting are needed.

## First complete flow

**Use a disposable SD card. The test overwrites the first `--total-bytes` bytes
plus packed metadata when `--integrity` is enabled, including any partition table
or vault header in that range. For 4 MiB of data, authenticated mode touches
4,474,368 physical bytes.**
Both the host flag and the protocol configuration explicitly enable scratch
writes. All operations are bounded to the configured first 1–8192 sectors.

A small first test:

```sh
python3 tools/board_bench.py --port /dev/serial/by-id/DEVICE \
  --allow-scratch-write --stacks aes --batch-bytes 4096 --total-bytes 65536 \
  --label 'Board 1 / disposable card model' \
  --firmware build-pico/fuse_vault_v2_bench.uf2 \
  --output results/v2-first-flow.json
```

A matrix using defaults (five stacks, five batch sizes, 4 MiB per configuration):

```sh
python3 tools/board_bench.py --port /dev/serial/by-id/DEVICE \
  --allow-scratch-write --label 'Board 1 / disposable card model' \
  --firmware build-pico/fuse_vault_v2_bench.uf2 \
  --output results/v2-matrix.json
```

For each configuration, the runner:

1. Prepares public test keys on the board once and initializes SD.
2. Sends deterministic bytes in batches; each acknowledgment follows encryption
   and completed SD writes. No routine payload readback occurs in the write path.
3. Requests the same range back, decrypts on the board, and compares every byte
   on the laptop. Generation of test data occurs before timing.
4. Ends the session, syncs and clears prepared keys, then records the outcome.

Authenticated configuration formats the metadata region to unset before each
case. This initialization time is logged in the configuration response, outside
the transfer phases. It does not initialize the full payload.

The same data/range is reused between configurations. Tests measure a small
repeated scratch workload, not full-card steady-state or filesystem throughput.
If interrupted, reports retain completed cases and partial counts for the current
case with `complete: false`. Commands are never retried automatically. After a
transport timeout/framing failure, physically reconnect before starting again:
a pending write may already have reached the card.

## Results

The console prints one verified result per configuration. JSON also includes:

- Host, run label, seed, timestamp, optional UF2 SHA-256 and port identity.
- Cipher order, batch bytes, total/completed bytes and request counts.
- Key setup time, encryption/decryption time and ciphertext SD read/write time.
- HMAC time, metadata I/O time, and metadata sectors read/written.
- Actual physical footprint, including metadata.
- `transaction_seconds`: summed host request-to-response durations, including
  USB, framing, worker handoff and acknowledgment. Read duration includes receipt
  of all returned bytes, but excludes the subsequent laptop comparison.
- `kib_per_second`: completed payload KiB / transaction seconds.
- `wall_seconds` and `wall_kib_per_second`: entire phase including laptop loop,
  bookkeeping and read verification.
- Verification result and final sync result. `--log-batches` adds per-request
  timings and LBAs for detailed analysis.

USB-only time is not measured directly. The difference between host transaction
time and board crypto/HMAC/data/metadata time also contains framing, queueing and host overhead;
do not label it pure USB transfer time. Raw runs report zero crypto time.

The pipeline currently processes one complete batch at a time. Core 0 services
USB while core 1 works, but USB payload transfer, crypto and SD are not yet
pipelined across multiple outstanding batches. This establishes a measurable
baseline; later overlapping buffers can be compared against it. CDC uses bulk
USB endpoints; the nominal serial baud rate is not its actual transfer rate.
These rates are not a prediction of USB MSC/SCSI or filesystem performance.

## Verification state

Desktop tests cover cipher correctness, SD batch/error contracts, the real
benchmark engine, scratch bounds, malformed frames, failure handling, partial
host I/O, and a complete Python-runner-to-C-engine flow backed by a temporary
file. The integration matrix verifies multiple stack orders/batch sizes including
short final batches and detects deliberately altered returned bytes.

Release and address/undefined-sanitizer tests pass; LeakSanitizer is disabled in
this traced environment. The RP2354 ELF and UF2 build. The current image uses
approximately 98 KiB code and 72 KiB BSS, excluding runtime stack high-water
measurements. Core 1 has a 16 KiB stack allocated in main RAM.

**First hardware validation passed on 2026-09-16.** Board `317741A1459A6F94`
completed the USB handshake, a 64 KiB AES smoke test, and all 25 stack/batch
configurations (4 MiB written and verified per configuration). See the
[hardware baseline](../../results/v3-hardware-baseline.md) and its raw logs.
This establishes the tested flow on this board/card, not broader reliability
or performance with authentication/metadata.


## Authenticated-storage comparison (protocol 5)

Reflash the newly built UF2 before using this runner. Older protocol replies are
shorter; the runner rejects their version before reading the remaining header. Disconnect/reconnect after flashing.
The USB ID remains cafe:4021, but the binary protocol version is checked.

Start with one stack and a small range, adding `--integrity`:

```sh
python3 tools/board_bench.py --port /dev/serial/by-id/DEVICE \
  --allow-scratch-write --integrity --stacks aes --batch-bytes 4096 \
  --total-bytes 65536 --firmware build-pico/fuse_vault_v2_bench.uf2 \
  --output results/v5-auth-first-flow.json
```

Then run the desired matrix once without and once with `--integrity`, saving
separate reports. Use the same board, card, firmware, stacks and batch sizes.
The runner verifies the returned bytes in both modes. Authenticated reads check
all requested written tags before decryption; corruption is board status 5.

The authenticated hardware smoke test and all 20 paired protocol-4 configurations
passed on 2026-09-16. See the [paired comparison](../../results/v4-authenticated-baseline.md).
Historical V3 results remain an unauthenticated baseline.
See [layout and cache details](../../src/storage/README.md) and
[device-state placement](../../docs/v2-security-state.md).


## SHA accelerator comparison

The default firmware now uses `FV_HMAC_PICO=ON`. First flash the new UF2 and run
`--info-only`. It must report `hmac_backend: pico-sha256-cpu-fed` and
`hmac_self_test: true`. The check compares hardware tags against a known vector
and the software reference before permitting SD operations.

Rerun the same five stacks at 4 KiB and 32 KiB batches, 1 MiB each:

```sh
python3 tools/board_bench.py --port /dev/serial/by-id/DEVICE \
  --allow-scratch-write --integrity --batch-bytes 4096 32768 \
  --total-bytes 1048576 --label 'SHA accelerator comparison' \
  --firmware build-pico/fuse_vault_v2_bench.uf2 \
  --output results/v5-sha-auth-matrix.json
```

Compare HMAC time as well as end-to-end throughput with
`results/v4-auth-matrix.json`. For a same-protocol software reference, configure
another build directory with `-DFV_HMAC_PICO=OFF`, flash its UF2 and repeat the
same command/report setup. Backend selection is a build option, not an SD format
change. The 25 MHz SD clock, XTS algorithms, full tags and metadata layout remain
the same. The on-board self-check and all ten accelerated configurations passed
on 2026-09-16. HMAC time fell about 84%; an AES encryption timing regression
limits write gains in this build. See the [measured comparison](../../results/v5-sha-comparison.md).

See [backend implementation and tests](../../src/crypto/backends/README.md).

## AES timing diagnostic

The whole-firmware `copy_to_ram` diagnostic failed to enumerate on USB after
flashing and power cycling. No performance measurements were obtained.

The narrower replacement is `build-pico-aes-ram/fuse_vault_v2_bench.uf2`, built
with `-DFV_AES_RAM=ON` and normal flash startup. This optional diagnostic moves
only AES block encryption/decryption into SRAM; hardware HMAC remains enabled.
The option defaults OFF. The build and linked addresses are verified. Board startup, HMAC self-check
and AES/Camellia authenticated round trips passed. AES encryption improved
from 1,054 to 634 ms/MiB, but Camellia slowed in the new layout; this remains
a diagnostic rather than a final optimization. See the
[investigation and build commands](../../results/v5-aes-investigation.md).

## Both ciphers in SRAM

Firmware builds now default to `FV_CIPHERS_RAM=ON` (desktop builds remain OFF).
This places AES block encrypt/decrypt, Camellia ECB/Feistel and its four round
lookup tables in SRAM using the SDK startup copy mechanism. Startup, USB,
key setup and pipeline orchestration retain their normal placement. AES lookup
tables already reside in RAM. Hardware SHA remains enabled.

Ready artifact: `build-pico-ciphers-ram/fuse_vault_v2_bench.uf2`.
Build with the usual firmware CMake command, using that build directory and
`-DFV_CIPHERS_RAM=ON -DFV_HMAC_PICO=ON`; convert the ELF with the documented
picotool command. To reproduce historical flash/AES-only builds, explicitly
set `-DFV_CIPHERS_RAM=OFF` (and select `FV_AES_RAM` as required).

The linked initialized RAM increases from 15,440 to 19,200 bytes: **3,760 bytes**.
All selected routines and tables have verified SRAM addresses; the four
Camellia tables match the previous image byte-for-byte. All eight desktop
suites pass. All 30 board cases passed across three complete matrix runs
on 2026-09-16, with stable cipher timings. See the
[repeated hardware benchmark](../../results/v5-ciphers-ram-benchmark.md).

After flashing, run three separate full-matrix passes, preserving each report:

```sh
for run in 1 2 3; do
  python3 tools/board_bench.py \
    --port /dev/serial/by-id/usb-Fuse_Vault_Fuse_Vault_V2_PIPELINE_317741A1459A6F94-if00 \
    --allow-scratch-write --integrity --batch-bytes 4096 32768 \
    --total-bytes 1048576 --log-batches \
    --label "Both ciphers in SRAM, repeat $run" \
    --firmware build-pico-ciphers-ram/fuse_vault_v2_bench.uf2 \
    --output "results/v5-ciphers-ram-matrix-$run.json" || break
done
```

This covers raw plus AES, Camellia and both stack orders at two batch sizes.
Compare per-stage timings and the spread across repeats before broadening to
longer transfers, other batch sizes and hardware interruption/corruption tests.
