# Double-buffered MSC firmware — 2026-09-20

Artifact: `build-pico-device-ui/v2_device_ui_pipeline.uf2`
SHA256: `a66dd9949bde3b7a648dbd04c41227d8d76437436183027abedce262c9fb11d1`
Highest UF2 payload end: `0x10025300`; all payloads below journal at `0x101fe000`.

- Two 32 KiB buffers: USB endpoint + worker scratch, 56 KiB more than before.
- ELF size: text 158252, data 0, BSS 198444 bytes.
- Main RAM static allocation ends at `0x2003465c`; heap limit `0x20080000`.
  About 302 KiB remains between these addresses (not a worst-case runtime audit).
- Writes overlap next USB receive with crypto/authentication/SD; final command
  status waits for the last durable write. Reads prefetch within command bounds.
- Existing vault format, credentials, OTP and flash journal unchanged.
- SDK TinyUSB source untouched; pinned build-time command/reset extension.

Validation:
- All 25 host tests passed normally and under ASan/UBSan.
- Affected USB tests rerun after final reset handling changes; all pass in both builds.
- Real patched TinyUSB BOT driver tested with a delayed worker at 4, 16 and 32 KiB
  under ASan/UBSan: next receive while worker pending, no premature successful CSW,
  read prefetch ownership, late failure and BOT reset drain/wipe all pass.
- Firmware build and UF2 address-range validation pass.
- LeakSanitizer disabled because it cannot run under this sandbox's ptrace;
  AddressSanitizer and UBSan remain enabled.

Not flashed by this task. Real-device copy/checksum, reconnect, error handling and
throughput comparison remain the next hardware step. No speed gain claimed from
host timing. See `docs/v2-usb-pipeline.md` for implementation and test procedure.
