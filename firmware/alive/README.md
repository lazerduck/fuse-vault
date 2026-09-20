# Minimal vault-board USB startup test

This target defaults to the **custom RP2354A vault board**. A separate `pico2`
build supports the standard Pico 2, omitting all vault-specific GPIO setup and
using the SDK's standard Pico 2 flash layout. Use the matching UF2 for each board.
It follows Raspberry Pi's USB hello-world pattern: `pico_stdlib`, SDK USB stdio,
`stdio_init_all()`, and a single foreground loop. SDK 2.3.0 commit used:
`98a542c1a62fb549ffb5d66a3e5892b06276b670`.

## Behaviour

- SDK startup, clocks, flash execution, USB descriptors and USB task servicing.
- Core 0 only. No custom startup hooks, watchdog, clock wrappers or embedded XIP
  setup override. CPU clock remains the SDK's normal RP2350 150 MHz setting.
- USB CDC product `Fuse Vault ALIVE`, SDK VID:PID `2e8a:0009`.
- Every second: `FV_ALIVE_V1 AWAKE <uptime-ms>`.
- Receiving `?` replies `FV_ALIVE_V1 PONG <uptime-ms>`.
- No crypto, SD driver, vault, flash journal operations, OTP provisioning or core 1.
- SDK USB reset commands are disabled. Use the physical BOOTSEL button to reflash.

The application does not configure GPIOs on either board. The user confirmed that
on the vault board the display is disconnected and hardware defaults select and
enable USB-C. **Connect only USB-C; leave USB-A disconnected.** The earlier test
explicitly drove the mux and backlight; the revised no-GPIO build removes those
operations. Both boards now use identical main.c, with board-specific SDK settings
and the existing vault journal reservation retained.

## Build

```sh
cmake -S firmware/alive -B build-pico-alive \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico-alive -j4
/tmp/fv-picotool-usb/picotool uf2 convert \
  build-pico-alive/fuse_vault_alive.elf \
  build-pico-alive/fuse_vault_alive.uf2 --family rp2350-arm-s --platform rp2350
```

The linker retains the last 8 KiB journal reservation. UF2 payload bounds must
remain below `0x101fe000`. Flashing changes the application; the program does not
clear or migrate existing vault state. The SDK may use ROM services internally,
including retrieving a USB serial identity; this is not an OTP-free boot path.

## Controlled hardware test

1. Flash `build-pico-alive/fuse_vault_alive.uf2` in BOOTSEL. Leave connected.
2. Find its `/dev/serial/by-id/` entry and run:
   `python3 tools/alive_probe.py /dev/serial/by-id/<the-ALIVE-device>`.
3. Record the post-flash result separately.
4. Completely unplug, wait five seconds, reconnect normally without BOOTSEL.
5. Repeat the same probe and record the cold-start result.

A pass requires a PONG and two advancing heartbeat timestamps. This establishes
that main, the timer, USB receive and USB transmit work. It does not test storage
or crypto. No USB device means the failure could precede main or be in USB/routing;
this minimal firmware does not claim to pinpoint the failing instruction.

## Startup documentation review

Primary sources:

- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
  sections 5.2.7, 5.2.8.1, 5.3 and 14.3.
- [Official USB hello-world source](https://github.com/raspberrypi/pico-examples/tree/master/hello_world/usb).
- Installed SDK `src/rp2_common/pico_crt0/crt0.S` and
  `src/rp2_common/pico_runtime_init/runtime_init_clocks.c`.

RP2354 contains an RP2350 die and a Winbond W25Q16JVWI flash die. Its in-package
flash uses the same boot mechanism as external flash. The ROM searches flash for
a valid image definition and enters it with a working flash read mode. Additional
XIP setup is optional; **omitting PICO_EMBED_XIP_SETUP is not itself a bug**.
The existing board header's generic boot2 selection does not require enabling it.

BOOTSEL USB also needs the crystal and a 48 MHz USB PLL clock. Working BOOTSEL
therefore argues against a completely nonfunctional crystal, but does not prove
reliable application clock switching or electrical margins. Core 1 remains asleep
until explicitly launched. Post-download and normal starts must be tested separately.

In the installed SDK, crt0 initializes data/BSS and enters runtime initialization
before main. The clock initializer starts XOSC, initializes system and USB PLLs,
configures clock sources, and starts ticks. The previous retained checkpoint after
PLL initialization does not prove which later instruction failed, nor establish
that this new minimal image will fail in the same place.

## Status

Build and image checks are recorded in `results/minimal-alive-20260919.md`.
Hardware post-flash and cold-start checks are pending; no startup fix is claimed.

## Standard Pico 2 comparison

Build separately so the original vault artifact remains available:

```sh
cmake -S firmware/alive -B build-pico2-alive \
  -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico2-alive -j4
/tmp/fv-picotool-usb/picotool uf2 convert \
  build-pico2-alive/fuse_vault_alive.elf \
  build-pico2-alive/pico2_alive.uf2 --family rp2350-arm-s --platform rp2350
```

Flash `build-pico2-alive/pico2_alive.uf2` only onto the standard Pico 2. Leave the
vault board unplugged during this comparison. The USB product/protocol is the
same; the serial number distinguishes the boards. Use the same host probe,
first after flashing and then after a complete power disconnect/reconnect.

A cold-start pass on Pico 2 shows this minimal application and SDK can cold-start
on standard hardware. It does not by itself distinguish vault board configuration,
flash/chip differences, USB routing, or electrical problems. A failure on both
points us toward shared software or host/cable factors, without proving either.
