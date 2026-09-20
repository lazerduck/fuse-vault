# Minimal ALIVE firmware — 2026-09-19

- Release build passed with SDK 2.3.0 for fuse_vault / rp2350-arm-s.
- ELF size: text 31,468 bytes; data 0; BSS 2,640 (not total runtime RAM).
- UF2: 54784 bytes, 107 blocks.
- Payload bounds: 0x10000000..0x10006b00 (end exclusive).
- Every payload checked below the reserved journal at 0x101fe000.
- SHA256: 738b3995076729cbd4d228a3fd5cc1b90d32158e143837d1c4d79d3c99d04a5a.
- Symbol checks: no embedded XIP call, boot trace hook or core-1 launch.
- Host probe passed Python compilation and CLI-help checks.
- Hardware flash/handshake and cold-start validation pending.

See firmware/alive/README.md for reviewed documentation, exact scope and test order.
No firmware was flashed and no attached device state was changed during this build.

## First hardware test: post-flash PASS

User reported ready after flashing. USB enumerated as 2e8a:0009,
product Fuse Vault ALIVE, serial 317741A1459A6F94 (bus 5/address 101).
The host probe exited successfully:

```text
FV_ALIVE_V1 PONG 56707
FV_ALIVE_V1 AWAKE 57003
FV_ALIVE_V1 AWAKE 58003
PASS: ping answered and heartbeat uptime advanced
```

This confirms application execution, USB receive/transmit and advancing timer
uptime after flashing. Cold-start behaviour is not yet established. Only the
single-byte '?' ping was sent; no storage or OTP commands were issued.

## Second hardware test: cold-start handshake unavailable

After the user reported complete unplug/replug, elevated lsusb showed neither
2e8a:0009 ALIVE nor BOOTSEL. No serial/by-id directory existed; the probe refused
to open an unidentified device and sent no ping. Kernel log recorded the working
ALIVE device disconnecting at 11:31:28, then a UCSI error at 11:31:34, without a
new USB enumeration in the checked interval. This reproduces the post-flash /
cold-start difference with the minimal SDK USB application and fixed USB-C mux.
It does not establish whether the CPU reached main or whether power/USB routing
failed. Storage, crypto, core-1 launch, authority recovery and dynamic presence
mux logic are not required to reproduce the observed missing-USB outcome.

## Standard Pico 2 comparison prepared

Built the same heartbeat/ping loop with PICO_BOARD=pico2 and rp2350-arm-s in
build-pico2-alive. Vault GPIO setup is compiled out; stock Pico 2 linker layout
is used. Release build passed. picotool confirms pico2, SDK 2.3.0, ARM Secure,
image 0x10000000..0x10006a00. Disassembly confirms main enters stdio_init_all
without vault GPIO setup. Artifact: build-pico2-alive/pico2_alive.uf2.
SHA256: c996026c92a268621d60815cf19a44d8c538f0d413fb17769cf7ceea035b57e9.
Hardware validation pending; original custom-board UF2 retained unchanged.

## Standard Pico 2: post-flash PASS

USB ALIVE serial 26B14A6EB7B34616 enumerated on bus3/address14.
Probe returned PONG at 47973 ms and AWAKE at 48005 and 49005 ms, exit 0.
The first post-flash handshake passed. Cold-start test remains pending.

## Pico 2 cold-start and identical-image comparison

The standard Pico 2 (serial 26B14A6EB7B34616) passed the subsequent cold-start
probe: PONG 36871, AWAKE 37000 and 38000 ms. User clarified that the vault display
is disconnected and hardware defaults route USB-C without application GPIO setup.
A no-GPIO vault variant was built locally but not flashed by the assistant; user
stopped that direction in favour of testing the same Pico 2 image on both boards.

User then reported flashing the Pico 2 test firmware onto the custom board,
without unplugging afterward. Custom board serial 317741A1459A6F94 enumerated
as ALIVE on bus5/address103 and passed: PONG 40163, AWAKE 41003 and 42003 ms.
Firmware identity is user-reported; the ALIVE protocol does not distinguish
builds and no readback was performed. This establishes post-flash operation on
the custom board; its cold-start result with this image is still pending.

## Custom board, user-reported Pico 2 image: cold-start USB absent

After unplug/replug, no ALIVE or BOOTSEL USB identity and no serial port appeared.
Probe exited 2 at identity checking, before sending a ping. Kernel log records
ALIVE disconnect at 11:38:30 and UCSI error at 11:38:37, without subsequent device
enumeration in the checked interval. Thus the same user-reported test image
passes post-flash on both boards, cold-starts on standard Pico 2, but yields no
USB on the custom board after cold power-on. This is not proof of a PCB defect:
chip/flash differences, persistent boot state and connection/power remain possible.
Host paths differed (Pico 2 on 3-2, custom board on 5-1), so this is not a fully
controlled electrical/host comparison. No new build or device mutation performed.

## Second custom sample: post-flash PASS

User supplied a new, reportedly clean custom sample with ALIVE already flashed.
Identified serial 66ED2A91873CF67F, distinct from the previous sample, enumerated
as 2e8a:0009 on bus5/address105. Probe passed with PONG 63846 ms, AWAKE 64004 and
65004 ms. No OTP inspection/provisioning, flash writes or SD commands were run;
clean state is user-reported, not independently inspected. Cold-start check pending.

## Second custom sample: cold-start PASS

After the requested unplug/replug, sample 66ED2A91873CF67F enumerated as ALIVE
on bus5/address106. Probe exited 0: PONG 18987 ms, AWAKE 19000 and 20000 ms.
This confirms a successful cold-start handshake on a second board of the custom
design, using the same host bus/port family as the failing original sample.
It argues against an unavoidable design-wide failure with this minimal test.
The difference may be original-unit hardware or persistent state; neither is
established. Exact flashed-image equivalence remains user-reported. No OTP,
flash or SD mutation was performed. Next useful investigation is a read-only
comparison of original and clean board boot configuration and flash contents.

## Recheck and V2 startup-only preparation

Clean sample 66ED2A91873CF67F again passed ALIVE: PONG 52560, AWAKE 53000
and 54000 ms. User requested V2 boot testing without actions. Built existing
V2 deferred-worker mode in build-pico-v2-startup-only, with STARTUP=ON,
BOOT_TRACE=OFF, ENROLLMENT=OFF, OTP_INSPECT=OFF. No source changes to V2.
Only BOOT will be queried; START is not authorized by this test. Worker recovery
will remain stopped. Build passed; UF2 payload stays below journal.
Artifact: build-pico-v2-startup-only/v2_startup_only.uf2
SHA256: 92aeb1c005c1559fc4fe47e042afe6b5c405dbee84a541d9af97dae64d440b1f.
Hardware flash and post-flash/cold-start BOOT tests pending.

## Clean sample V2 startup-only: post-flash PASS

Sample 66ED2A91873CF67F enumerated cafe:4022 on bus5/address109 after user flash.
Only BOOT was sent. Response: ok=true, stage=0, start_requested=false,
sense_a=0, sense_c=1, sense_a_duplicate=0, sense_c_duplicate=1.
V2 core-0 USB startup works after flashing; worker remains stopped. No START,
provisioning, OTP write, SD operation or authority recovery was requested.
Cold-start test pending.

## Clean sample V2 startup-only: cold-start PASS

Following the requested full unplug/replug, sample 66ED2A91873CF67F enumerated
cafe:4022 on bus5/address110. Only BOOT was sent; response ok=true, stage=0,
start_requested=false, sense_a=0, sense_c=1, duplicates=0/1. V2 core-0 startup
and USB therefore pass cold boot on this sample with the worker deferred.
No START or authority/OTP/SD operation was requested. This does not validate
normal V2 worker startup, recovery or storage. Together with the ALIVE results,
it shows the original board's missing USB is not universal to the custom design
or V2 core-0 startup. Original-unit hardware versus persistent-state cause remains
unresolved; no provisioning of the clean sample has been performed in these tests.
