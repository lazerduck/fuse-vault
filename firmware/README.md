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

### File-backed OTP emulator

Host builds include a deliberately small OTP model backed by an 8 KiB file.
It represents all 4096 logical ECC data rows as little-endian 16-bit values.
New images contain only zeroes; programming may change bits only from zero to
one, each row is flushed independently, and the image persists across process
restarts. It therefore exercises the firmware's empty, partially programmed,
active, invalid, and revoked layouts without claiming to reproduce OTP timing,
analogue failure modes, ECC fault correction, permissions, or page locking.

The accompanying tool generates test roots from the host operating system and
never prints them:

```sh
firmware/build-host/fuse_vault_otp_file_tool /tmp/fuse-vault-otp.bin status
firmware/build-host/fuse_vault_otp_file_tool /tmp/fuse-vault-otp.bin provision
firmware/build-host/fuse_vault_otp_file_tool /tmp/fuse-vault-otp.bin status
firmware/build-host/fuse_vault_otp_file_tool /tmp/fuse-vault-otp.bin revoke
```

Deleting the image creates a new emulated device on the next invocation. This
is intentionally possible only in the host model; real OTP cannot be reset.
Equivalent inspect, provision, and revoke commands are available in the VS Code
task list and use `firmware/build-host/emulated-otp.bin`.

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

Keyboard press and release events now pass through the same input controller as
the RP2354 GPIO buttons. It applies a 25 ms debounce, a 450 ms hold delay, and a
120 ms repeat interval. Each application state registers only the controls it
accepts and whether they may repeat; changing screens while a control is held
requires release before the new binding can fire. See
[`docs/firmware-architecture.md`](../docs/firmware-architecture.md).

Secret entry is a reusable component shared by setup, confirmation, and
unlocking. The setup method screen offers four methods:

- **Number wheels:** three 0-99 wheels. Left and Right choose a wheel, Up and
  Down change it, and OK completes entry.
- **Direction sequence:** D-pad directions append to a 6-16 step sequence,
  Back removes the last step, and OK completes a sufficiently long sequence.
- **Numeric keypad:** a navigable 3x4 keypad provides digits, delete, and OK.
  PINs contain 4-12 digits; Back also deletes the most recent digit.
- **Word list:** four positions each select one of 64 stable words. Left and
  Right choose a position, Up and Down choose its word, and OK completes entry.

Pressing Back on an empty variable-length entry returns to the preceding
screen. Setup keeps the first entry in transient memory and confirms it by
comparing its canonical encoding with a separately entered value. The selected
method remains active after setup. On a persistent restart, the boot-recovery
boundary validates the complete vault header and translates its stable method
identifier before supplying the recovered method to `fv_app_init`; missing or
invalid provisioned metadata fails closed.

Every method produces a fixed-capacity, zero-padded canonical encoding with a
format version, method identifier, value count, and method-specific values.
The credential-envelope backend consumes the complete encoding through two
independently salted, bounded work functions; the encoding is never treated as
an encryption key. These human-entered methods rely on the persistent attempt
limit and device-held secret rather than standalone password entropy.

In VS Code, run `Fuse Vault: Run display simulator` from **Tasks: Run Task**.
Use `Fuse Vault: Run persistent simulator` to exercise attempt persistence
across restarts without manually acknowledging counter writes.
For breakpoints and stepping, select `Fuse Vault: Debug display simulator` in
the Run and Debug panel and press F5. The build is configured automatically
before either action.

Pass `--unprovisioned` to start in first-time setup. The simulator injects
authentication and persistence outcomes because those platform services do not
exist yet; it never exposes a real or plaintext storage volume.

The unprovisioned simulation walks through entry-method selection, initial
secret entry, independent confirmation, mismatch handling, and acceptance of
the destructive-lockout/no-recovery policy. Without persistent development
storage, press `O` after the provisioning screen appears to inject successful
platform provisioning. With `--state-dir`, the provisioning command runs the
credential coordinator: it creates and verifies device roots and a credential
envelope before publishing the provisioned security-state record. The confirmed
setup secret remains transiently available through the canonical setup encoding
only while the provisioning backend needs it and is cleared on completion or
fault.

Run the graphical simulator with persistent development attempt state using:

```sh
firmware/build-host/fuse_vault_display_simulator \
  --unprovisioned \
  --state-dir firmware/build-host/simulator-state
```

`--unprovisioned` supplies the initial state only when the directory is empty;
after provisioning, the recovered security state and vault-header method take
precedence on every restart. Omitting it for an empty directory fails closed
instead of synthesizing incomplete provisioned metadata.

In this mode the simulator creates a development-only device secret using the
operating system random source and stores it separately from vault metadata.
Provisioning writes the secret record first, reads it back, and writes a
separate active marker last. Destructive lockout writes a revocation marker;
after that marker exists the host backend will neither reveal nor replace the
secret through its API.
Attempt records use two alternating, atomically replaced slots: submitting a
secret persists the incremented counter before authentication can begin, and
the newest valid sequence is recovered after restart. CRC32 detects corruption
and incomplete records but is not cryptographic authentication. These local
files contain a plaintext simulated device secret and must never be treated as
a production vault or copied into device firmware.

The RP2354 boot path uses the same recovery boundary, but deliberately supplies
no vault-header service yet: the physical redundant SD header backend is not
implemented. Consequently an otherwise provisioned hardware build remains
locked in the fault state rather than assuming the wheels method. Credential
authentication is compiled as portable core code but remains disconnected on
hardware until that storage service is available.

A limited host NOR utility tests the invariants needed by the future RP2354A
internal-flash attempt journal: aligned erase/program operations, the inability
to change programmed zero bits back to one without erase, and torn programming.
It is a fault-injection aid, not a hardware simulator, and does not model flash
timing, XIP/cache effects, wear, or electrical failure.

The portable authenticated journal itself is now implemented and shared with
the RP2354A build. It uses two erase sectors and append-only 256-byte records,
keeps the latest valid record safe while rotating sectors, and authenticates
each record with two 32-byte tags supplied by the platform cryptography layer.
Host tests interrupt writes at every byte boundary, corrupt the newest record,
and force sector rotation. Their deterministic tag function is intentionally
non-cryptographic.

The RP2354A flash adapter reserves the final 8 KiB of stacked flash, enforces
page/sector alignment and bounds, performs writes through Pico SDK safe-flash
coordination, and verifies programmed or erased contents. Link-time and boot-time
overlap checks keep firmware out of the reserved range; boot fails locked if the
geometry is not safe.

The dual authenticator is now implemented: independent 256-bit device roots
feed SP 800-108 HMAC-SHA-256 and KMAC256 derivations, and both full-size tags
must validate. The SHA branch uses RP2350 hardware acceleration and the Keccak
branch is portable C. Host tests include NIST's published KMAC256 sample and an
independently generated HMAC derivation/tag answer. Hardware journal writes
are enabled only after active roots have been recovered; the boot path connects
that root set to journal recovery.

The shared first-time security-state transaction now generates both roots and
a vault ID, erases and writes the first authenticated journal record, reads it
back, and only then commits the OTP active marker. It finally discards the RAM
copy, reads the roots back through the selected OTP backend, and authenticates
the journal again. The same transaction is covered with the file-backed OTP
emulator and is compiled for the RP2354A. It is not yet dispatched from the UI:
the vault-header wrapping and encrypted-storage stages must be placed before
its irreversible final commit so setup cannot mark an unusable vault complete.

The two-root OTP lifecycle backend now reserves user-data page 60 and uses ECC
rows for both roots and separate format, active, and revocation markers. Root
data is read back before the active marker is programmed, interrupted
provisioning is permanently invalid rather than retried, and revoked roots are
never returned. Ordinary firmware cannot provision roots; that operation is
available only to an explicitly configured provisioning build. Enabling it
requires both `-DFUSE_VAULT_ENABLE_OTP_PROVISIONING=ON` and the CMake cache
confirmation
`-DFUSE_VAULT_OTP_PROVISIONING_CONFIRMATION=I_UNDERSTAND_OTP_WRITES_ARE_PERMANENT`.
Persistent page locks and access permissions remain deferred until
sacrificial-board testing.
The hardware boot path now accepts either a completely empty root page or a
fully active root set with a valid authenticated journal. Every other state
fails locked. Temporary roots are cleared after deriving journal keys. Attempt
reservations now append to the hardware journal before authentication can
continue, while destructive lockout programs the one-way revocation marker and
clears the derived keys.

The credential-envelope backend generates a random 32-byte VMK and protects it
with an inner AES-256-GCM envelope and an outer Ascon-AEAD128 envelope. The
independent wrapping keys come from PBKDF2-HMAC-SHA-256 plus Root A and iterated
KMAC256 plus Root B. The 92-byte result fits in the existing vault header, whose
security-relevant metadata is authenticated as AEAD associated data. Host tests
require the correct entry and both roots, mutate every envelope byte, exercise
metadata tampering and work limits, and include the official empty-message
Ascon-AEAD128 known answer. Production iteration counts remain unset pending
measurement on the assembled RP2354A.

The core refuses to begin authentication until an incremented attempt counter
has been persisted, and it refuses to request USB mass-storage attachment until
successful authentication and persistence of the reset counter. The tests exercise
provisioning, vault and FIDO mode selection, unlock and lock, persistent attempt
limits, fault handling, and the MSC attachment boundary.

The shared authentication coordinator canonicalizes the active entry, validates
the persisted header and method, reads both device roots, and opens the envelope.
Credential and method mismatches are ordinary failed attempts; missing, corrupt,
or unreadable security material faults closed. A successful VMK belongs to an
explicit session until lock, eject, or fault requests session-key erasure, while
the coordinator workspace is cleared before every return.

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
├── include/fuse_vault/security_journal.h
│                              Portable authenticated journal interface
├── src/app.c                 State machine and UI view model
├── src/main.c                RP2354A platform entry point
├── src/rp2354_security_flash.c
│                              Reserved stacked-flash journal backend
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
