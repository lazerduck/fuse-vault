# Testing revision-1 boards without a screen

Branch: `codex/headless-usb-debug`.

## Start here

1. Disconnect the physical screen. Its supply is directly wired to the PCB;
   disabling display firmware cannot make the reversed connector safe.
2. Hold BOOTSEL while connecting USB-C and copy
   `firmware/build-headless/fuse_vault.uf2` onto the boot drive.
3. Run from a terminal in the desktop session:

   ```sh
   python3 /home/adam/projects/fuse-vault/firmware/tools/usb_screen.py
   ```

4. The viewer finds the debug firmware automatically. Press the physical
   D-pad, OK and Back buttons; the window mirrors the real firmware's 160x80
   framebuffer at 4x size. There are no simulated button presses or simulated
   storage in this viewer.
5. For storage tests, use a disposable SD card. The normal setup flow can erase
   it and create permanent device roots in OTP. The headless option does not
   disable root creation, the attempt limit, or destructive lockout. Use test
   credentials and data. No secure-boot/page-lock provisioning is added here.

The viewer uses Python 3 and GTK 3/PyGObject, already available in this Linux
workspace. Run it in a graphical desktop session. On another Debian/Ubuntu
machine the packages are `python3-gi` and `gir1.2-gtk-3.0`. PySerial is not needed.
Use `--port /dev/ttyACM0` to override automatic discovery if necessary.
If opening the serial device reports permission denied, check that the desktop
user has access to that device (commonly membership in `dialout`, followed by
logging out and back in); do not run the entire viewer as root.

## What this build changes

- Development-only USB CDC serial interface is available before unlocking and
  remains connected through lock, eject and application faults.
- MSC shares the same USB configuration, with no medium available until the
  normal authenticated storage-mode attachment. Lock/eject removes that medium
  and follows the existing key-clearing runtime path.
- Physical display SPI is disabled; display signal pins are inputs and the
  backlight control is held off. The normal view renderer supplies USB images.
- Real board buttons, SD, cryptography, OTP and flash-journal code still run.
- VID/PID is development identity `cafe:4013`, product `Fuse Vault SCREEN DEBUG`.
- FIDO2 and release builds are rejected when the headless option is enabled.
- Connector presence/conflict checks remain active. A connector change or
  conflict disables the mux; power-cycle with one connector to recover.

This profile deliberately exposes whatever the device screen displays,
including secret-entry/review screens. It is not a production security profile.
It does not expose arbitrary memory, keys, or host control commands.

## Reading the diagnostics

The viewer displays the app state, framebuffer sequence, uptime, physical
button bitmask and initial boot checks. Boot checks cover input initialization,
flash interface, OTP interface, SD driver, USB, connector routing and composed
services. An SD-driver OK result does not establish that a card is present or
readable. Boot recovery is reported separately as ready, waiting for media,
or failed. These checks are an initial snapshot, not continuous health tests.

KDF and SD operations still run synchronously. The image may pause during
those operations; the viewer reports that it is waiting. This first version
is a screen mirror and boot diagnostic, not a trace of every internal operation.
If initialization hangs before the USB service loop, no frame will arrive.

No serial device at all: confirm the headless UF2 was flashed, connect USB-C
only, and check OS USB enumeration. A failed connector-presence check can
prevent the application from routing USB even though ROM BOOTSEL works.

## Rebuilding

```sh
cmake -S firmware -B firmware/build-headless \
  -DPICO_SDK_PATH=/home/adam/pico-sdk \
  -Dpicotool_DIR=/home/adam/projects/fuse-vault/firmware/build/_deps/picotool \
  -DFUSE_VAULT_HEADLESS_DEBUG=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build firmware/build-headless -j4
```

The picotool argument reuses the existing local tool on this machine, avoiding
another download; omit or replace it on other machines. The normal build folder
and its configuration remain separate.

## Wire format and verification

Host sends ASCII `f` to request a snapshot. Firmware returns eight little-endian
32-bit words: magic bytes `FVD1`, payload length 25600, frame sequence, app-state
ID, physical button mask, boot-check bitmask, boot recovery, uptime milliseconds.
The payload is 160x80 RGB565 pixels, little-endian, row-major. Requests during
transmission are discarded. A separate snapshot buffer prevents tearing while
USB applies backpressure. Disconnect cancels the current transfer. The viewer
requests up to twenty frames per second, waiting for each response.
Actual rate depends on USB and any synchronous SD/crypto work. The bench
firmware services USB without the normal 1 ms main-loop sleep; the viewer
drains available serial chunks every 5 ms.

Host CTests cover partial USB writes, zero transmit capacity, coherent snapshots
while the displayed frame changes, disconnect/reconnect, physical display pin
safety, and MSC lock/eject/reattach with USB remaining initialized. Python tests
cover fragmented/coalesced reads, invalid-length resynchronization and RGB565
conversion:

```sh
ctest --test-dir firmware/build-host --output-on-failure
python3 firmware/tools/usb_screen_tests.py
```

At implementation time: 35 host checks and 4 viewer protocol tests pass; both
normal and headless RP2354 builds link. Actual USB enumeration, GTK window
rendering and end-to-end board operation still require the user's desktop/board.

## Storage performance investigation (2026-09-14)

The first board showed about 24 KiB/s completed writes, with FAT32 creation
lasting 15 minutes 35 seconds. This is not an accepted performance baseline.
The current headless build uses RelWithDebInfo (-O2 with debug symbols), rather
than Debug (-Og). This is compiler optimisation, not a production release or
an alteration of encryption, sector layout, or durability guarantees.

The original profiling request `g` returns five counters. This preserves the FVD1
header but increases payload length from 25600 to 25680. After the pixels,
five records each contain four little-endian uint32 words: operation count,
total microseconds low word, total microseconds high word, maximum microseconds.
Categories are SD sector read, SD sector write, SD sync, encrypted sector read,
and encrypted sector write. The totals include failed operations. Counters
start at boot and are not reset by locking. Vault timings include their nested
SD operations; do not add all category totals together. Other vault time
includes cryptography, validation, memory work and instrumentation overhead.
The on-card format is unchanged. Old viewers can still request `f` without the
extension. The updated viewer sends `hg`: new firmware accepts `h` and
ignores the following `g` while transmitting; older profiling firmware ignores
`h` and accepts `g`. Screen-only firmware falls back to `f`, with periodic
profiling retries. A timeout never disables previously confirmed metrics;
timings older than two seconds are labelled stale.

Request `h` returns 13 counters (25808 payload bytes). The first five retain
their original order, followed by: select existing copies, read-back
verification, record nonce KMAC, layer IV KMAC, AES-XTS including key setup,
ChaCha20, Ascon encrypt, Ascon decrypt. Selection and verification include their
nested SD and crypto operations; do not sum them with those components. AES
and ChaCha timings combine encryption and decryption calls. Counts show how
many times each layer runs, rather than assuming the selected preset. Timing
rows scroll in the viewer.

For a useful comparison, capture counters after unlock, copy one small test
file (for example 1 MiB) and wait for the host to finish writing, then capture
again. Capture before and after the slow delete separately. Differences in
count and total time isolate each workload; the displayed means are cumulative
since boot. This build does not automatically run a benchmark or alter data.

Finish or safely stop the current host operation before reflashing. Then close
and relaunch the updated viewer, unlock the existing vault, and use normal
small file operations. Capture the timing panel after slow operations. This
identifies whether physical card commands or work above the SD layer dominate
before changing buffering, transport or cryptography. No automatic disk writes,
benchmarks, formatting or reflashing are performed by the viewer.

The SD transfer implementation currently configures two DMA channels even for
single-byte status polling; sector payloads are already transferred in bulk.
That is a candidate optimisation, not yet a measured root cause or changed
transport. This performance build adds measurements rather than weakening
read-back verification or copy-on-write recovery.


### Prepared KMAC optimisation

The storage nonce and layer-IV paths now prepare the fixed KMAC prefix and
key state once per unlocked session. Each computation copies that state before
absorbing the original per-record message and output length. For these short
messages this reduces three Keccak permutations to one. It does not change
keys, nonce inputs, ciphertext format, authentication, recovery or read-back
verification. Prepared state is key-equivalent and lives inside the encrypted
block/pipeline owners, whose existing lock/fault clearing erases it.

The original one-shot implementation remains in use for other KMAC callers
and as the differential test reference. Tests cover 1,728 combinations of
key/customization, message and output lengths, including rate boundaries,
repeated use without mutation, invalid initialization, erasure, and the
existing encrypted-record golden values and interrupted-write recovery tests.
Device speedup has not yet been measured. Compare record-nonce and layer-IV
means against the previous 1.33/1.31 ms and total vault writes against 19.90 ms
using the same scheme/card and a similar workload. No SD transport changes
are included in this comparison.


### Further performance build

`firmware/build-headless/fuse_vault-performance.uf2` includes prepared KMAC,
constant rho/pi rotations in Keccak (same permutation/round count), and a
16-entry SD CRC lookup (same polynomial and checking). The KMAC-only image
remains at `firmware/build-headless/fuse_vault-kmac-cache.uf2` for comparison.
The Keccak change was checked against the pre-change implementation across
the 1,728-case corpus; its concatenated output SHA-256 is
`fe57451d323042359b48a5e9f128c6e7e3f255fb3b2e5802e500365b9c1dceb6`.
SD protocol tests use an independent bitwise CRC reference and cover corrupted
reads, command status, transport failure and recovery. The 8 MHz SD clock,
DMA/PIO transport and write ordering are unchanged.

After obtaining a full frame, the viewer sends `ihg`. Firmware handling `i`
returns only the 208 timing bytes when the last transmitted frame sequence
still matches, or a full frame plus timings when it changes. The same FVD1
header carries fresh state, buttons and uptime. Reconnect invalidates the
firmware's frame history, and the viewer starts each connection with `hg` to
force a full frame even if the device did not observe the disconnect. Older
firmware ignores unsupported request letters. Legacy `f`, `g`, and `h`
responses retain their formats.

An unchanged screen now uses 240 rather than 25,840 bytes per response:
about 4.8 KB/s instead of 517 KB/s at 20 responses/second. These are protocol
traffic calculations, not measured storage throughput. New firmware and a
restarted updated viewer are both required for the reduction. Host tests
exercise short writes, unchanged/changed frames, and disconnect recovery;
viewer tests cover fragmented and coalesced full/timings-only packets.

No measured device speedup or unlock-time improvement is claimed until the
hardware is retested. Password KDF iteration counts are unchanged. Compare
with the same card and Scheme 3, and retain the before/after timing panels.

### Native four-bit SD bench build

Build separately to preserve the SPI comparison images:

```sh
cmake -S firmware -B firmware/build-native-sd \
  -DPICO_SDK_PATH=/home/adam/pico-sdk \
  -Dpicotool_DIR=/home/adam/projects/fuse-vault/firmware/build/_deps/picotool \
  -DFUSE_VAULT_HEADLESS_DEBUG=ON -DFUSE_VAULT_SD_NATIVE=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build firmware/build-native-sd -j4
```

This selects native four-bit data at 25 MHz and multi-sector commands, using
PIO1 and two DMA channels. It retains the encrypted on-card format, KMAC
optimisations, authenticated read-back and synchronous write completion. It
supports sector-addressed SDHC/SDXC cards; smaller byte-addressed SDSC cards
are rejected. Production builds reject the native option until validated.

Finish file operations and eject before flashing. **Fully unplug all USB
power after flashing, then reconnect:** the card must leave the old SPI mode
through a power cycle. No format or vault reprovisioning is required. Restart
the viewer; its status line should show `4-bit SD / 25 MHz`. Boot flag bit 7
identifies the native backend. With this flag, the SD counters are labelled
read/write **request**, not sector: a request can contain multiple sectors.
The existing vault-sector timing categories remain comparable across builds.

Test reading an existing file before copying a new small test file. Capture
vault timings before host file operations and after copy/eject. Successful
builds and mock tests do not validate PIO electrical timing on the actual card.
The SPI performance and KMAC-only UF2s remain available for recovery/comparison.

No change to CPU-core allocation is included. This build isolates the larger
transport change so measured gains and any card/transport errors are attributable.
