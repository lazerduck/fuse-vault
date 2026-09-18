# RP2354 security bring-up firmware

A separate **development image** linking the actual portable vault lifecycle to
four-lane SD, SRAM cipher code and Pico SHA. Core 0 services USB; core 1 owns
crypto, entropy, SD and device authority. USB identity is `cafe:4022`, product
`Fuse Vault SECURITY DEBUG`, distinct from the existing pipeline benchmark.

## Available bring-up tests

- Read-only OTP snapshots: all 64 pages' blank/programmed/all-ones/unreadable row
  counts, raw public lock rows, lock-read errors and current software locks.
- Laptop snapshot comparison: decoded permanent Secure/Non-secure/bootloader
  locks, key-selection settings, redundant-copy agreement and occupancy changes.
- TRNG-backed AES-256 CTR-DRBG, with built-in health checks enabled, source-error
  propagation, repeated/zero/all-ones block checks and fresh reseeding per request.
- Full envelope seal/unlock/wrong-credential timing at a chosen PBKDF2 count.
- Destructive SD test: create a two-layer AES/Camellia vault, unlock, write 1 MiB,
  lock/reopen/read/verify, change credential, reject the old credential, then
  reopen and verify all data using the new credential. Batches are 32 KiB.

The persistent path stores root/token in fixed application OTP pages and attempts/
header authority in an authenticated flash journal. See the
[allocation, record format and debug workflow](../../docs/v2-persistent-authority.md).
These operations are implemented and host-tested; physical OTP programming and
power-cycle verification remain pending. No automatic initial provisioning occurs.

The separate `vault-test` command still uses RAM-only enrollment and now refuses
an already provisioned device. All test credentials are public literals. No VMK,
root, token, entropy bytes or raw data-row contents are returned. The existing
benchmark image/protocol remains separate.

## Build and remove debug commands

```sh
cmake -S firmware/security -B build-pico-security \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release -DFV_DEBUG_OTP_INSPECT=ON \
  -DFV_DEBUG_ENROLLMENT=ON
cmake --build build-pico-security -j4
v1/firmware/build/_deps/picotool/picotool uf2 convert \
  build-pico-security/fuse_vault_security.elf \
  build-pico-security/fuse_vault_security.uf2 --family rp2350-arm-s --platform rp2350
```

The archived picotool is reused only as a build executable. An installed picotool
can be substituted. Source dependencies stay in V2 / the configured Pico SDK.

`FV_DEBUG_OTP_INSPECT` and `FV_DEBUG_ENROLLMENT` default **OFF**. The OFF build
excludes inspection and the prepare-flash/provisioning/create/check/change/wrong/destroy command
handlers; binary symbols and command strings were checked. Internal final-attempt
destruction remains enabled. This entire image is a debug harness: disabling these
options alone does not make it a production application. There are no arbitrary
OTP-row, raw-memory, secret-export or software-lock-change commands.

## First-run commands

Flash `build-pico-security/fuse_vault_security.uf2` using BOOTSEL, then reconnect
normally. Use its `/dev/serial/by-id/usb-Fuse_Vault_Fuse_Vault_SECURITY_DEBUG_...`
port (the tool validates the distinct USB VID/PID).

```sh
python3 tools/security_probe.py --port "$PORT" info
python3 tools/security_probe.py --port "$PORT" snapshot --out results/otp-before.json
python3 tools/security_probe.py --port "$PORT" rng
python3 tools/security_probe.py --port "$PORT" kdf --iterations 60000
python3 tools/security_probe.py --port "$PORT" vault-test --erase-sd
```

The last command destroys the SD layout: headers at LBA 0/8, metadata at 2064,
and 2,048 payload sectors. It leaves the private-object reservation untouched,
but the card's previous partition/filesystem layout is no longer usable. No writes
occur from info/snapshot/rng/kdf themselves. On boot, an enrolled device can finish
a previously pending final-attempt destruction; virgin devices are never enrolled
automatically.

The KDF command is a configurable timing diagnostic. Enrollment and the vault
self-test now use the agreed **60,000 iterations**; persistent header acceptance
bounds are exactly 60,000. Previous hardware timing measured about 1.40 seconds.

For persistent provision/create/power-cycle/check/rotation, follow the
[persistent workflow](../../docs/v2-persistent-authority.md#debug-workflow).
Capture a new snapshot after provisioning or advancement:

```sh
python3 tools/security_probe.py --port "$PORT" snapshot --out results/otp-after.json
python3 tools/security_probe.py diff results/otp-before.json results/otp-after.json
```

Snapshot files are created exclusively, so a baseline cannot be overwritten by
mistake. Diffs reject different device identities. An unreadable row/page is not
reported as blank, erased or securely destroyed. Occupancy is aggregate: programming
more bits in an already-programmed row may leave counts unchanged. Lock rows show
individual redundant copies so partial lock transitions can be seen. These tools
do not by themselves verify irreversible destruction or authorize an allocation.
Factory/boot/key-reserved pages must be identified before selecting application slots.
No data-row hashes are exported as an indirect secret fingerprint.

The protocol is bounded ASCII requests and JSON responses, one command at a time.
Malformed/oversized framing requires reconnect. A timeout does not retry a command;
a running SD test can continue after USB disconnect and erases its temporary keys
when it finishes. None of these commands is intended as a production host API.

## Entropy implementation and remaining work

`src/platform/rp2354/entropy.c` uses chain 0, sample period 100 system clocks and
normal VN/CRNGT/autocorrelation checks. Each 192-bit collection has a one-second
bound; health failure/timeout aborts the operation. These development parameters
follow [RP2350 datasheet §12.12](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
`pico_rand` is not linked or used. Mbed TLS CTR-DRBG consumes 48 entropy bytes for
initialization/reseeding and reseeds every generation request. Its standard
self-test runs before bring-up use. Diagnostics return status/timings only.

This is an implemented candidate entropy adapter, not a claim of characterized
min-entropy, certification or hardware-tested reliability. The first board run passed the source smoke test; qualification still includes temperature,
clock and supply conditions and failure recovery policy. There is no fallback to
SDK PRNG, serial numbers, timers or test seeds on entropy failure.

The current allocation is root page 16, token pages 17–24, with flash offsets
0x1fe000–0x1fffff reserved by both V2 firmware linker layouts. Commands recheck live
contents/permissions and verify writes. Permanent access locks are deliberately
unchanged during development. Hardware interruption testing, Secure access/debug
policy and protected RAM isolation are tracked in the
[production checklist](../../docs/production-checklist.md).

More passing RNG smoke tests show reliable integration, not entropy quality. We
rely on vendor source design and focus remaining checks on failure handling and
the intended board operating conditions.

## Validation

- 19 desktop tests pass, also under ASan/UBSan. They include entropy failures,
  OTP inspection contracts, CLI wrong-device/debug-option guards, byte-cut journal
  failures, token lifecycle and a full vault pipeline using the persistent model.
- ARM builds succeed with both debug options enabled and both disabled. The debug
  ELF has 129,484 bytes BSS, including a 32 KiB worker stack and 32 KiB data buffer.
  Physical runtime stack verification remains a release task.
- [First hardware run, 2026-09-17](../../results/security-bringup-20260917.md): five
  RNG diagnostics, three 60,000-iteration envelope timing runs and three complete
  SD lifecycle runs passed. Median unlock: 1.40151 s. This used the previous
  RAM-authority firmware; OTP snapshots matched and no OTP writes were performed.
- The new persistent image still needs flashing, real provisioning and power-cycle
  tests. Host failure injection does not prove physical OTP/ECC behavior.

## Reused-board initialization (2026-09-18)

The first persistent-image connection showed blank root/token OTP pages but a
failed authority open. No provisioning was attempted. The updated image adds
occupancy fields to `state` to distinguish stale flash from an OTP/read failure,
and an explicit `prepare-flash --confirm-device ID` command guarded by entirely
blank/readable enrollment OTP. See the
[hardware run record](../../results/security-persistence-20260918.md). The cause
is not yet confirmed; a new flash is needed to run those diagnostics.

## Startup isolation image

`FV_DEBUG_STARTUP=ON` (default OFF) delays worker launch and authority recovery
until an explicit `START` command. This is diagnostic-only: it deliberately delays
pending-policy recovery and must not appear in production. Build output lives in
`build-pico-security-startup/`; journal reservation and OTP allocation are unchanged.

Use `python3 tools/security_startup.py --port "$PORT" boot` for core0 startup stage
and USB A/C sense inputs; `start` requests the worker. Neither unlocks. START runs
normal authority recovery, which can complete an already pending destruction.
Normal `security_probe.py` commands work after stage 14. Stages: 0 awaiting START,
1 core0 flash-lockout setup, 2 core1 launch, 10 worker entered, 11 worker lockout
ready, 12 authority adapter ready/open starting, 13 open finished/recovery starting,
14 worker ready. The existing USB mux selection/interlock remains unchanged.
