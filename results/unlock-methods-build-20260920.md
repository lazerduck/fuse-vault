# Unlock methods build

Artifact: `build-pico-device-ui/v2_device_ui_methods.uf2`
SHA-256: `8015b13bab4efad13ff8299d35886aa40f9a9f40dc5566407a9389f9d17dbb94`
Highest payload end: `0x10023b00`; authority journal is preserved.

Implemented setup method selection, visible direction arrows, four 00–99 code
wheels, and four words through the stable V1 64-word directional tree. New profiles
3 and 4 have canonical four-byte encodings and preserve profiles 1/2. Unlock method
is selected from the flash-anchored header without spending an attempt. Changing
method from the unlocked menu verifies the current credential and rewraps the
existing VMK, preserving files, cipher stack, policy and OTP token slot.

Validation: full 25-test desktop and ASan/UBSan suites passed; affected five tests
rerun after final adjustments. Covers all 64 word selections, wheel wraparound,
confirmation, cancellation, profile bounds, unchanged VMK/file data/token across
method changes and rejection of forged profile hints without attempt commits.
Method, wheel, word-tree/review and visible-pattern framebuffers visually inspected.
ARM firmware build passed. No board flash or OTP/storage operations performed.

Board acceptance: flash, unlock the existing pattern vault, unmount filesystem,
choose Change unlock method, enter current credential and confirm a new one. Verify
unlock and files after cold reconnect; do not use Erase and set up merely to change
method. New profiles are unsupported by older firmware. Release checklist remains.
