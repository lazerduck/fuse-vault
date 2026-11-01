# Fuse Vault — lock screen

[Firmware UF2](fuse-vault-lock-screen.uf2) · [Screen preview](lock-screen.png)

Built 1 November 2026 from `fa0af24c21dd667f1d39af98c54aa260604df498`
plus the lock-screen changes in this workspace. This includes the preceding
[security-settings firmware](../firmware-20261101-security-settings/README.md).

The locked screen now uses framed choices, a vault graphic, an amber locked
indicator and a cyan centre action. Its layout matches the controls:

- Up, at the top: Passkeys only.
- Select, in the centre: Full access.
- Down, at the bottom: Files read only.

Left/Right flips the screen and directions follow the displayed orientation.
Without FIDO support, the top action is omitted and the other controls retain
their positions. Existing button mappings, credential flows and vault formats
are unchanged. The existing vault can be used without re-enrollment.

## Local validation

- Both existing UI suites passed with ASan/UBSan: `device_ui_and_capacity`
  and `ui_components` (`ASAN_OPTIONS=detect_leaks=0`).
- Actual renderer previews inspected with FIDO enabled/disabled and in both
  orientations; preview compilation uses `-Wall -Wextra -Werror`.
- Release firmware built for the existing `fuse_vault` RP2354 board with
  TFT/UI, USB storage and FIDO enabled, 32 KiB USB buffers and all debug switches
  Off. SDK 2.3.0; ARM Secure image ends at `0x1007df80`, below the authority
  journal at `0x101fe000`.
- ELF: 522,108 bytes text, 0 data, 368,128 BSS. The layout adds 616 bytes of
  text and no static RAM relative to the security-settings image.
- `git diff --check` passed. No device was flashed; physical display acceptance
  remains for the v3 board.

UF2 SHA-256:

```text
a69196d2a3592c315cf7106f0a53d991363b921fbf003802cedc57f078f84093
```
