# USB mass storage implementation — 2026-09-20

Artifact: build-pico-usb-storage/v2_usb_storage.uf2
SHA256: f84f2c61108d45b42d1dcd7945765f8fdfcc93056a7bca493403e3e0d9e7c79e
UF2 payload end (exclusive): 0x10024900; all blocks below journal 0x101fe000.
SDK 2.3.0, Release, fuse_vault, RP2350 ARM Secure.
MSC and DEBUG_SESSION enabled; enrollment, OTP inspection, deferred startup and
boot trace disabled. Normal recovery/worker initialization retained.

Implementation: CDC + MSC; starts locked, debug public-fixture unlock, persistent
vault read/write with 4 KiB batches, serialized worker ownership, error senses,
eject/reset/suspend lock and buffered plaintext clearing. Host helper rejects
wrong USB identity/capabilities and refuses lock on locally mounted filesystems.
No vault formatting, provisioning or device operation was performed in this turn.

Validation:
- 21/21 desktop CTest tests pass, including MSC callbacks and bus invalidation.
- 21/21 pass under AddressSanitizer/UBSan with ASAN_OPTIONS=detect_leaks=0.
  Initial LeakSanitizer runs failed because this environment uses ptrace; leak
  checking is therefore not claimed.
- MSC firmware and the existing non-MSC worker-check firmware build successfully.
- Host session helper Python compilation and CLI-help checks passed.
- git diff --check passed.
- Real host enumeration, block I/O, formatting, lock/unlock and cold-start remain
  pending physical flash. No throughput claims; callbacks synchronously wait for
  worker completion. See firmware/security/USB_STORAGE.md for limitations.
