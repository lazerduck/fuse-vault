# Fuse Vault firmware

This directory contains the C firmware for the custom Fuse Vault RP2354A board.
It uses the Raspberry Pi Pico C/C++ SDK and contains the portable product core,
the RP2354A platform composition, native simulation, and verification tests.

The firmware enters locked and exposes no USB data interface. The GPIO map and
FSUSB42 OE/SEL behavior and active-high USB presence dividers are captured for
PCB revision 1. The screen mounts horizontally with its flex through the PCB;
its P-channel backlight switch enables low. Display initialization and SD
card-detect polarity remain gated pending assembled-board bring-up.

The application state machine and 160x80 RGB565 framebuffer renderer are
portable C shared by the RP2354A target, a native simulator, and automated
tests. Consequently, the simulator displays the same pixels that will be sent
to the TFT rather than recreating the interface with desktop widgets. Platform
code—not the application core—owns secrets, persistent security state, USB,
storage, input, and display hardware.

The SD card uses the standard SD memory-card SPI protocol on the existing
CLK/CMD/DAT0/DAT3 wiring. PIO0 drives mode-0 SPI timing; two DMA channels move
transmit and receive bytes, including whole 512-byte payloads. The fixed SPI
peripheral cannot map CLK=GPIO4, MOSI=GPIO5 and MISO=GPIO6, so PIO preserves the
PCB wiring. The display continues to use its separate SPI0 peripheral.

Initialization is limited to 400 kHz (divider rounded down in frequency), with
at least 74 initial clocks after a power-settling delay. After initialization
and CSD validation, data transfers use at most 8 MHz. Command CRC7, data CRC16,
capacity parsing and read-back verification remain in place. DMA/PIO waits have
a 100 ms deadline; a transport failure stops the state machine and DMA before
returning, invalidates the card and follows the existing storage-fault path.
Reinitialization resets the transport and returns to the slow clock.

The driver claims one PIO0 state machine, two instruction words and two DMA
channels, and releases them on deinitialization. Resource exhaustion fails
initialization. Calls remain synchronous on core 0: PIO/DMA removes CPU-driven
bit timing and byte copying, but does not add background USB/UI scheduling.
The candidate clock rates still require logic-analyser and card testing.

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

The candidate ST7735S display turns on by default, so orientation, offsets and
colour order can be inspected during bring-up. `FUSE_VAULT_ENABLE_DISPLAY=OFF`
is available for transport diagnostics (normal application startup requires a
working display). Enabling the display does not mark its profile as validated.
Bench builds can supply `FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL`,
`FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL`, and
`FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL` as `0` or `1`. Measured values must be
recorded in the board definition before release.

A production-candidate configuration additionally sets
`FUSE_VAULT_RELEASE_BUILD=ON`. That profile requires an optimized Release build,
a semantic version, an assigned non-development USB VID/PID, an explicit review
acknowledgement, and all display/SD/USB evidence gates in the board definition.
It rejects every bench-only override and a disabled display, and runs the OTP
policy verifier in release mode. It therefore fails today by design until the
signing hashes, recorded evidence, and assembled-board measurements have been
reviewed. A successful release build is still unsigned; use the
read-only packaging workflow in `../provisioning/README.md` to sign and verify
the ELF and recovery UF2.

## Host simulator and tests

Build the portable firmware core without the Pico SDK:

```sh
cmake -S firmware/host -B firmware/build-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

For AddressSanitizer and UndefinedBehaviorSanitizer verification, configure a
separate host build with `-DFUSE_VAULT_ENABLE_SANITIZERS=ON`. Environments that
run tests under `ptrace` must invoke CTest with
`ASAN_OPTIONS=detect_leaks=0`; address and undefined-behavior checking remain
enabled, while leak checking must be run on an unrestricted host.

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
Escape) maps to the Back button. Clickable controls use the same debounce and
repeat handling. The simulated-host note field accepts normal text input;
click a device control to return keyboard focus to navigation.

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
- **Word list:** four initially empty positions each select one of 64 stable
  words. Choices sit above, right, below, and left to match the D-pad. Each press
  chooses a group of 16, a group
  of four, then a word. Every word takes exactly three presses, and selection
  advances to the next empty position. Group labels show the first and last
  three-letter word prefixes. After all four words are chosen, arrows edit the
  corresponding numbered position and OK submits the reviewed phrase. Back
  undoes a group choice, cancels an edit, or removes the previous word at the
  root. Back at the first empty position leaves the screen. Held buttons do
  not repeat. Incomplete phrases cannot be submitted or encoded; completed
  phrases retain the existing word IDs and canonical encoding.

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

The graphical simulator now always uses persistent development storage and
the shared `fv_device_runtime`. Setup, authentication, attempt accounting,
encryption-stack selection, encrypted block access and teardown run for real.
There are no success/failure injection shortcuts in this window. The separate
terminal simulator remains an event-driven state-machine inspection tool.

With no arguments, the default device is stored under
`$XDG_DATA_HOME/fuse-vault/simulator/default` (normally
`~/.local/share/fuse-vault/simulator/default`). First launch enters setup;
subsequent launches recover that device. Use `--state-dir DIRECTORY` to keep a
named device elsewhere. Only one simulator may open a given device at a time.

`--new` starts a separate device; `--unprovisioned` remains an alias.
With an explicit state directory, existing provisioned state always wins and
is never erased. The window's **New device** button creates a uniquely named
sibling directory, leaving the previous device available via `--state-dir`.

A useful ergonomics session:

1. Complete setup using the clickable D-pad/OK/Back or keyboard controls.
2. Choose an entry method, enter and confirm its secret, choose the ordered
   cipher stack, then accept the no-recovery policy.
3. Select Vault and enter the same secret to unlock.
4. Type a short sample note in the simulated-host panel and click **Save**.
5. Click **Restart**, unlock again, then **Load** to retrieve that note.
6. Try a wrong secret, Back, Lock and Eject; try another method with **New device**.

The note panel accesses logical sector zero exclusively through virtual MSC and
the encrypted block adapter, with sync on save. It is a 511-byte UTF-8 sample
payload, not a filesystem or an OS-mounted drive. Lock/restart/eject disables
the panel and clears its displayed text. The backing media file stores the
encrypted record. Simulated roots remain ordinary local files, so use test
secrets and test notes. This environment does not emulate physical OTP locks
or device timing; cryptographic work runs synchronously on the host CPU.

**Restart** performs orderly detach and starts recovery using the same files.
Power-cut fault injection remains in automated tests. **New device** creates
fresh simulated roots on setup and lets you compare password methods without
deleting earlier devices. Keyboard holds use the firmware debounce/repeat
controller; loss of window focus releases held controls.

The RP2354 boot path now composes OTP roots, the internal flash journal, the raw
SD backend, authenticated media layout and redundant header store through the
same platform-service boundary as the host tests. It validates journal/media
vault identity before recovering the entry method and fails locked on any
missing, corrupt, unsupported, or inconsistent provisioned metadata.

The SD driver may initialize successfully with no card present. New devices can
therefore reach setup and retry after insertion. Provisioned devices show a
media-required state with USB disconnected, initialize a card on its insertion
edge, and continue only after the journal identity and authenticated vault
header match. Removal, low-level SD failure, encrypted-block failure, or MSC
sync failure during a live session is promoted to the application fault path so
USB disconnects and session keys are erased.

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

The same firmware handles first setup and normal operation. On a fresh device,
setup generates, commits and reads back two random OTP roots before creating
the vault header and journal. No provisioning build or second flash is needed.
Setup reuses existing active roots. Only a completely empty root/revocation
layout permits creation; partial, invalid, revoked or unreadable OTP never
causes regeneration. Recoverable SD or journal failures never revoke roots.

The OTP lifecycle uses page 60 for roots, format and active markers, and page 59
for revocation. Root data is verified before the active marker is committed.
Boot accepts empty roots for first setup, or active roots with an empty journal
for new vault setup. Provisioned state requires a matching authenticated SD
header. Attempt reservations append before authentication; destructive lockout
programs the one-way revocation marker and clears derived keys.

OTP access locks and firmware signing are separate from root creation. Candidate
page locks, a picotool-compatible permission file, secure-boot policy and signing
packager are defined in `../provisioning/`. Root creation must finish before
applying the permanent page-60 read-only lock. The application does not program
these locks or secure-boot settings automatically.

The credential-envelope backend generates a random 32-byte VMK and protects it
with an inner AES-256-GCM envelope and an outer Ascon-AEAD128 envelope. The
independent wrapping keys come from PBKDF2-HMAC-SHA-256 plus Root A and iterated
KMAC256 plus Root B. The 92-byte result fits in the existing vault header, whose
security-relevant metadata is authenticated as AEAD associated data. Host tests
require the correct entry and both roots, mutate every envelope byte, exercise
metadata tampering and work limits, and include the official empty-message
Ascon-AEAD128 known answer. The target starts at 100,000 PBKDF2 and 10,000 KMAC
iterations; assembled-device latency measurement must set final release values.

The core refuses to begin authentication until an incremented attempt counter
has been persisted, and it refuses to request USB mass-storage attachment until
successful authentication and persistence of the reset counter. The tests exercise
provisioning, vault unlock and lock, future-FIDO availability gating, persistent attempt
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

1. Verify the populated ST7735S module, candidate framebuffer orientation,
   offsets, colour order, SPI rate, and reset/backlight polarity.
2. Verify USB-A/USB-C presence polarity and bench-test the integrated
   disable/select/enable routing and disconnect-on-change behavior.
3. Bench-test SD CRC, geometry, removal, durability, and throughput on real cards.
4. Test TinyUSB MSC format/mount/read/write/sync/eject across supported hosts.
5. Supply signing identities and validate the frozen OTP, secure-boot, debug,
   rollback, and signed-update policy on sacrificial boards.
6. Run target KDF/stack/throughput/stack-watermark and power-cut campaigns.

See [`docs/product-readiness.md`](../docs/product-readiness.md) for the current
release checklist.
