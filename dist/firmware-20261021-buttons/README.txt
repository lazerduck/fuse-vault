Fuse Vault button diagnostic — 21 October 2026

Flash fuse-vault-button-diagnostic.uf2 using the usual BOOTSEL procedure,
then reconnect normally. This task has NOT flashed the device.

This dedicated diagnostic image reads GPIO10–15 with the existing external
pull-ups and polarity. Physical and injected UI button actions are disabled:
pressing Select will not start setup or approve a request. The screen stays
on its ordinary home screen; button results are viewed over USB.
Reflash normal firmware after testing to restore button navigation.

Monitor from the repository root (close any other serial viewer first):
python3 src/host/tools/button_monitor.py --device 16A9B46CD62975DC --seconds 120

Release all switches initially, then press each for at least half a second:
SW5 Up     GPIO10
SW6 Left   GPIO11
SW7 Down   GPIO12
SW8 Right  GPIO13
SW9 Select GPIO14
SW4 Back   GPIO15

HIGH/released is idle. LOW/PRESSED means asserted. Raw changes are sampled
by the firmware UI loop; stable states use 25 ms independent per-pin debounce.
The monitor polls every 50 ms and prints changes. Press counts since boot catch
stable presses between USB polls, including chords and inputs low at boot.
A count is an electrical assertion, not proof of a physical actuation.
Short transients between polls may not appear; this is not a logic analyser.

A pin LOW while untouched may be stuck, shorted or have a pull-up problem.
A pin that stays HIGH when pressed needs a connection/contact check.

Protocol: BUTTONS newline returns JSON schema 1, raw_pressed, stable_pressed,
and presses[6]. Bit/array order is Up, Left, Down, Right, Select, Back.
This is read-only telemetry, independent of screen orientation and UI actions.

Build: current working tree, Release, RP2350 ARM Secure, fuse_vault board.
FV_DEVICE_UI, FV_TFT_DISPLAY, FV_USB_MSC, FV_USB_FIDO, FV_DEBUG_SCREEN and
FV_DEBUG_BUTTONS enabled. FV_USB_IO_BYTES=65024. Enrollment, OTP inspection,
public-fixture session, deferred-start and boot-trace debug options disabled.
Existing uncommitted storage changes are included in this build.
Build directory: build-board-button-diagnostic.

Validation: full firmware compilation passed; simulated GPIO tests passed
for six inputs, debounce, held/released, simultaneous presses, boot-low and
noisy neighbour; monitor formatting/schema checks passed; existing UI test
passed. Picotool verified family rp2350-arm-s and image range
0x10000000–0x1007c67c, below reserved journal 0x101fe000.
Physical switch testing remains pending.

SHA-256: 442c02b532e116ec7c541b36bb25d50ee8cd0f85afa3aae9e5fed9c76df83e44
