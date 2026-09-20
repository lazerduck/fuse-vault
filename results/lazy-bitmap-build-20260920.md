# Lazy bitmap build — 2026-09-20

Artifact: `build-pico-device-ui/v2_device_ui_bitmap.uf2`
SHA-256: `16556170fdc678259111665731b2f4d144d53ff00c30e36a08b5d667ee4b0d8b`
Highest loadable UF2 payload end: `0x10022900` (journal starts 0x101fe000).

For the previously measured 122909696 physical sectors:
- Logical capacity: 115224146 sectors.
- Metadata: 7681610 sectors; bitmap: 1876 sectors (938.0 KiB).
- Setup writes only the bitmap, headers and persistent authority state; it does
  not clear the tag area or data area. No on-device timing measured yet.

Validation: 25/25 desktop tests and 25/25 ASan/UBSan tests passed with leak detector
disabled. Covers failures before/after every first-write write/sync operation,
crossing bitmap sectors, preservation of unrelated bits, dirty old SD contents,
forged state/tag rejection and independent legacy-envelope unlock/read/write/
credential-change compatibility. ARM UI firmware build passed. UF2 bounds checked.

No firmware flashed and no attached-board storage/OTP operations performed.
Existing volumes stay at layout 1; new creates use layout 2. For setup instructions
and wire-format details see docs/v2-lazy-metadata.md.
