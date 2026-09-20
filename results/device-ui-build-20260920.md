# Device UI development build — 2026-09-20

Artifact: `build-pico-device-ui/v2_device_ui.uf2`
SHA-256: `ddb7785ec2ac0f9320400fa0667bc730d315f496d5256adc3ed972e8b22dca1d`
Highest UF2 payload end: `0x10021a00`; reserved authority flash begins
`0x101fe000`. Every loadable UF2 block is below the journal.
ELF size: text 143812 bytes, BSS 139736 bytes (including worker stack).

## Implemented

USB-C-preferred mux; full-card capacity calculation using existing format;
firmware-owned 160x80 framebuffer and physical/debug input; confirmed direction
credential; 1–4 cipher layers; initial provisioning/creation; lock/unlock;
authenticated failure-policy changes; explicit destructive reset with token
revocation; GTK framebuffer viewer. FIDO area reserved, not implemented.

## Validation

- 23/23 desktop CTest checks passed.
- 23/23 ASan/UBSan checks passed with `ASAN_OPTIONS=detect_leaks=0`; affected
  UI/USB tests rerun after final changes, 4/4 passed.
- ARM development UI/MSC build succeeded with warnings treated as errors.
- ARM UI build with remote screen/input and fixture unlock disabled succeeded.
- Existing non-UI MSC firmware build succeeded.
- Viewer imports/bytecode/CLI and frame validation passed; GTK3 and Cairo present.
- Rendered setup review framebuffer inspected. No interactive desktop display
  available to this process (`Gtk.init_check` false), so live GTK interaction
  awaits the user's desktop run.
- No firmware flashed and no SD/OTP changes performed during this build task.

## Next board checks

Follow `firmware/security/DEVICE_UI.md`. Flash working sample 66ED2A91873CF67F,
open old test vault before starting the viewer, explicitly erase it and create the
full-card volume. Confirm host filesystem format, file/hash round-trip, cold boot
locked/unlock, persistent policy and mux power combinations. Retain old failing
sample unchanged. Debug flags remain enabled for this migration artifact.
