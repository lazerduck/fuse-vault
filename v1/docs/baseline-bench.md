# Baseline board measurements

**Current procedure: [integrated headless benchmark](integrated-baseline-bench.md).**
The separate bench image below failed to enumerate on the board despite verified
flash contents. Its fallback diagnostics did not recover evidence. It is retained
as investigation history; do not use it for further repeated flash attempts.
The commands below describe that older standalone attempt.

Prepared 2026-09-14. The standalone image builds; board results are pending.
This is the first measurement step of the approved storage redesign, not the
replacement storage firmware. Start with one board and one disposable card.

Startup revision: the first board attempt did not enumerate application USB.
The cause is not yet confirmed. The rebuilt image starts USB without initializing
SD, retries connector routing while presence inputs settle, and exposes `INFO`
before any card access. The runner now explicitly sends `INIT-SD` for card tests.
Reflash the rebuilt image before using the updated runner.

The follow-up flash readback matched the image, but application USB still did
not appear. A diagnostic revision records public startup state in 32 bytes of
uninitialized SRAM (`bench_boot_trace`, currently 0x20010000). It automatically
returns to ROM after ten seconds without USB mounting, or on a caught HardFault.
Do not power-cycle after this automatic return if collecting the trace.
Words are: magic 0x46564254, stage (1 main / 2 connector routing / 3 USB init /
4 USB service / 5 mounted), reason (1 timeout / 2 HardFault / 3 init rejection),
GPIO input bits, microseconds, CFSR, HFSR, reserved. A missing magic means the
trace was not established or did not survive; do not infer a startup stage then.
This temporary trace alignment consumes additional RAM address space and is not
part of the final product memory budget. The exact failure remains unconfirmed.

## What this image does

- Uses native 4-bit SD at 25 MHz and the current board routing definitions.
- Does not require a vault, password, OTP provisioning or a display.
- Does not program OTP or the internal security journal. Test crypto uses public
  fixed keys and only operates on synthetic data in RAM.
- SD is never exposed as a host disk. SD writes happen only on `ERASE-SD`.
- `ERASE-SD` overwrites the first 4 MiB of the inserted card three times,
  including partition and vault headers. The card's existing contents become
  unusable; this is not a secure-erasure procedure for the remainder of the card.
- The optional host disk is 128 KiB of RAM, identified as `Fuse Vault BENCH RAM`,
  USB development ID `cafe:4014`. It is not persistent or encrypted.
- Core 1 remains unused deliberately: establish the single-core baseline first.

## Equipment and first run

1. Label one spare board B01 and one disposable SD card C01. Record the card's
   manufacturer, model and advertised capacity. Disconnect other Fuse Vault
   boards initially to avoid ambiguity. A second board/card is useful later to
   check reproducibility, not required to begin.
2. Insert C01 with power disconnected. Connect USB-C only, through a known data
   cable. A working display is not required. Leave the previously damaged
   display disconnected; this firmware does not drive the display.
3. Use the board's usual BOOTSEL/ROM loading procedure and copy
   `firmware/build-bench/fuse_vault_bench.uf2` to its ROM USB drive.
4. Fully disconnect USB power and reconnect after flashing, especially if the
   card previously ran in SPI mode. Close the USB viewer and serial monitors.
   First check connectivity without card access:

```sh
python3 firmware/tools/run_baseline_bench.py \
  --info-only --label 'B01 USB startup check' \
  --output firmware/bench-results/b01-usb-startup.json
```

5. Run the following from the repository root. It performs crypto and raw SD
   reads, explicitly authorizes the destructive SD pass, then attaches the RAM
   disk. It saves host details, board ID, CPU clock, image hash and measurements.

```sh
python3 firmware/tools/run_baseline_bench.py \
  --label 'B01 C01: enter card model here' \
  --uf2 firmware/build-bench/fuse_vault_bench.uf2 \
  --erase-sd --ram-usb \
  --output firmware/bench-results/b01-c01-baseline-1.json
```

Omit `--erase-sd` for a read-only SD pass. Omit `--ram-usb` to leave the host
disk absent. No formatting, flashing or OTP operations are performed by this
script. If multiple bench boards are connected, specify `--port` using that
board's `/dev/serial/by-id/...` path. The script rejects other USB firmware IDs.

Each command has a five-minute timeout and is never automatically retried. A
timeout/disconnect produces an incomplete report, not a successful result.
Power-cycle before rerunning. Successful JSON rows print as each test finishes;
an individual test can take several seconds without producing a line.

## USB-only measurement

After `--ram-usb`, Linux should see an unformatted **128 KiB** USB disk. Do not
format it. Identify its whole-disk device by size, model and USB connection:

```sh
lsblk -o NAME,SIZE,MODEL,SERIAL,TRAN,MOUNTPOINTS
```

Use the identified device in place of `/dev/sdX` below:

```sh
sudo python3 firmware/tools/bench_ram_usb.py \
  --device /dev/sdX \
  --output firmware/bench-results/b01-c01-usb-1.json
```

The script requires the exact bench USB ID and 128 KiB whole-disk capacity,
opens exclusively and uses direct I/O to avoid benchmarking Linux's page cache.
It writes and reads 8 MiB per request-size/direction, recycling RAM addresses,
and checks the entire RAM disk after each size. Writes include a final fsync.
This measures the **current 512-byte MSC callback path**, including host/Python
overhead, not an absolute hardware limit or the future batched USB implementation.
Larger host requests currently still break into 512-byte callbacks.

Do not run serial or SD benchmarks concurrently with the USB measurement.
Power-cycle to return to an unattached disk and repeat the baseline three times
with distinct output filenames. Then repeat using B02/C01 to check board effects,
or B01/C02 to check card effects. Change one variable at a time.

## Interpretation

| Test | Measures | Excludes |
|---|---|---|
| `sd_read` | 4 MiB of raw card reads with 512 B / 4 KiB / 16 KiB driver requests | Vault crypto and host filesystem |
| `sd_write` | Same region and sizes; current backend completion plus final sync | Pattern generation, USB service between operations, verification |
| `sd_verify` | Separate complete readback/pattern check following each write pass | It is not part of reported write throughput |
| `ascon_*` | Encrypt/decrypt 1 MiB in 512 B units, 88 B associated data | Record nonce KMAC, storage layout, SD and USB |
| `pipeline_preset_*` | Current selected-layer pipeline including IV KMAC and per-sector cipher setup | Ascon, SD, USB, one-time pipeline initialization |
| RAM USB | Host-to-device path using synthetic RAM storage | SD and crypto |

Preset indices: 0 AES-XTS, 1 ChaCha20, 2 AES then ChaCha, 3 ChaCha then AES.
Crypto outputs are round-trip checked outside measured crypto calls. The
existing profiling calls remain active, so this is an instrumented baseline.
SD/crypto `us` sums the timed operations; the capture also reports command wall
time. Do not confuse either with full file-operation latency. Compute KiB/s as
`bytes * 1000000 / us / 1024`. Read and write directions have separate costs.

This short 4 MiB test locates overhead; it does not measure full-card steady
state, all card locations, endurance or worst-case tail latency. Repeat and
expand only where findings require it. Driver completion checks still exist in
this baseline; it measures our present raw backend, not an ideal SD controller.

The ELF currently reserves a 64 KiB main stack and has approximately 174 KiB BSS,
including the 128 KiB RAM disk and two 16 KiB SD buffers. These are link-time
figures, not measured runtime headroom for the full application or a second core.
Use the build map and stack-usage files as inputs to the later memory budget.

Next, use measured crypto and transport costs to finalize the packed format,
then compare the redesigned path against these baselines. A board-based KDF
calibration and actual UI-latency test belong to that next step. The known
12.27-second unlock capture remains the old KDF baseline.

Repeat the actual FAT32 **quick** format only on the new full-size exposed
volume, recording formatter/version, exact command, cluster size and elapsed
time. Do not compare formatting this 128 KiB RAM disk with the original card.

## Build and local verification

```sh
cmake -S firmware/bench -B firmware/build-bench \
  -DPICO_SDK_PATH=/home/adam/pico-sdk \
  -Dpicotool_DIR=/home/adam/projects/fuse-vault/firmware/build/_deps/picotool \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build firmware/build-bench -j4
python3 firmware/tools/run_baseline_bench_tests.py
```

The bench project is separate from the product CMake target. Host protocol tests
cover fragmented replies, disconnects, wrong replies and board selection.
Successful compilation and host tests do not establish successful board operation.
