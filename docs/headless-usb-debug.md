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
  -DFUSE_VAULT_HEADLESS_DEBUG=ON -DCMAKE_BUILD_TYPE=Debug
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

At implementation time: 34 host checks and 3 viewer protocol tests pass; both
normal and headless RP2354 builds link. Actual USB enumeration, GTK window
rendering and end-to-end board operation still require the user's desktop/board.
