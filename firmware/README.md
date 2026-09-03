# Fuse Vault firmware

This directory contains the C firmware for the custom Fuse Vault RP2354A board.
It uses the Raspberry Pi Pico C/C++ SDK and currently provides the minimal
application and board structure needed for hardware bring-up.

The initial firmware enters a locked idle state and exposes no USB interface.
Peripheral pin assignments will be added after they have been verified against
an exported schematic or the assembled board. The initial GPIO map has now been
captured from the schematic and remains subject to bring-up verification.

The application state machine and 160x80 RGB565 framebuffer renderer are
portable C shared by the RP2354A target, a native simulator, and automated
tests. Consequently, the simulator displays the same pixels that will be sent
to the TFT rather than recreating the interface with desktop widgets. Platform
code—not the application core—owns secrets, persistent security state, USB,
storage, input, and display hardware.

The SD card is connected using four-bit SDIO. Its initial driver will use the
RP2354A's PIO facilities. Higher layers access storage only through the generic
512-byte block-device interface, keeping the encrypted-volume and USB code
independent of SDIO. A later PCB revision or driver can therefore move to SPI
without changing the storage-security architecture.

PCB revision 1 accidentally connects each USB-presence signal to two GPIOs:
GPIO2/GPIO16 for USB-A and GPIO3/GPIO17 for USB-C. Firmware treats GPIO2 and
GPIO3 as canonical and must leave GPIO16 and GPIO17 configured as high-impedance
inputs.

## Prerequisites

- CMake 3.15 or later
- Ninja or Make
- Arm GNU embedded toolchain supported by the Pico SDK
- Raspberry Pi Pico SDK 2.3.0 or later
- `picotool` for inspecting and loading binaries (recommended)

On Ubuntu-based systems, install the build dependencies with:

```sh
sudo apt install build-essential cmake ninja-build python3 \
  gcc-arm-none-eabi libnewlib-arm-none-eabi \
  libstdc++-arm-none-eabi-newlib
```

Add `libusb-1.0-0-dev` if `picotool` should communicate with connected boards
over USB. It is not required to compile the firmware or copy a UF2 file to a
board in BOOTSEL mode.

Clone the Pico SDK, including its submodules, somewhere outside this repository:

```sh
git clone --branch 2.3.0 --recurse-submodules \
  https://github.com/raspberrypi/pico-sdk.git /path/to/pico-sdk
```

## Build

From the repository root:

```sh
cmake -S firmware -B firmware/build -G Ninja \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/build
```

The build produces `fuse_vault.uf2`, `fuse_vault.elf`, and related files in
`firmware/build/`.

The custom board definition selects the secure Arm Cortex-M33 platform, the
RP2350 A package, and the RP2354A's 2 MiB stacked flash. It uses the generic
serial-read boot stage until the flash configuration has been validated on the
assembled device.

## Host simulator and tests

Build the portable firmware core without the Pico SDK:

```sh
cmake -S firmware/host -B firmware/build-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

Run the interactive terminal simulator:

```sh
firmware/build-host/fuse_vault_simulator
```

On a desktop with GTK 3 development files installed, the host build also
produces a graphical 160x80 landscape display simulator:

```sh
firmware/build-host/fuse_vault_display_simulator
```

The graphical simulator displays the 80x160 panel rotated into the device's
160x80 landscape orientation at 4x scale. Arrow keys map to the four D-pad
directions, Enter (or Space) maps to the centre OK button, and Backspace (or
Escape) maps to the Back button. The window footer lists the additional letter
keys used to inject platform results that do not exist yet, such as a
successful or failed authentication.

The first secret-entry prototype uses three 0-99 combination wheels. Left and
Right choose a wheel, Up and Down change it with wraparound, OK submits the
combination, and Back cancels and clears it. This provides 1,000,000 possible
combinations (just under 20 bits); it is intended to be evaluated as a quick
device PIN protected by the persistent attempt limit and device-held secret,
not treated as sufficient standalone encryption-key entropy.

Every secret-entry method must produce a canonical, domain-separated encoding.
The current wheel encoding includes a format version, method identifier, wheel
count, and the three values. A future reviewed KDF backend will transform that
encoding into a fixed 32-byte unlock key using a per-vault salt and recorded
parameters. The canonical encoding is deliberately not treated as a key, and
the KDF/device-secret construction has not yet been selected or implemented.

In VS Code, run `Fuse Vault: Run display simulator` from **Tasks: Run Task**.
For breakpoints and stepping, select `Fuse Vault: Debug display simulator` in
the Run and Debug panel and press F5. The build is configured automatically
before either action.

Pass `--unprovisioned` to start in first-time setup. The simulator injects
authentication and persistence outcomes because those platform services do not
exist yet; it never exposes a real or plaintext storage volume.

The unprovisioned simulation walks through entry-method selection, initial
secret entry, independent confirmation, mismatch handling, and acceptance of
the destructive-lockout/no-recovery policy. Press `O` after the provisioning
screen appears to inject successful platform provisioning. The confirmed setup
secret remains transiently available through the canonical setup encoding only
while the provisioning backend needs it and is cleared on completion or fault.

The core refuses to request USB mass-storage attachment until authentication
succeeds and the reset attempt counter has been persisted. Failed attempts must
also be persisted before another attempt is accepted. The initial tests exercise
provisioning, vault and FIDO mode selection, unlock and lock, persistent attempt
limits, fault handling, and the MSC attachment boundary.

## Flash

For initial bring-up, hold the board's BOOTSEL control while connecting it and
copy `firmware/build/fuse_vault.uf2` to the RP2350 boot volume. Alternatively,
use a supported debug probe or `picotool` once the board connection and reset
arrangement have been confirmed.

## Layout

```text
firmware/
├── boards/fuse_vault.h       Custom Pico SDK board definition
├── host/                     Native simulator and tests
├── include/fuse_vault/app.h  Portable application interface
├── include/fuse_vault/block_device.h
│                              Protocol-neutral storage interface
├── src/app.c                 State machine and UI view model
├── src/main.c                RP2354A platform entry point
└── CMakeLists.txt            Firmware build definition
```

## Next steps

1. Confirm the oscillator, BOOTSEL, debug, and peripheral pin assignments.
2. Add a board bring-up build with explicit diagnostic output.
3. Implement display initialization and a test pattern.
4. Implement directional controls and button debouncing.
5. Detect the active USB connector and verify the data multiplexer.
6. Initialize the SD card over SDIO and exercise raw block reads and writes.
7. Add the locked-mode UI and explicit USB mode state machine.

Secure boot, OTP provisioning, destructive lockout, cryptographic storage, and
release signing will be introduced only after the basic hardware has been
validated.
