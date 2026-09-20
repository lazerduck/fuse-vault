# Metadata initialization progress — 2026-09-20

Artifact: `build-pico-device-ui/v2_device_ui_progress.uf2`
SHA-256: `fa9fbbdd2d294c011b07db1cfa2957b04a7938d179fd4285bd3aea40c019b7a7`
UF2 payload ends at `0x10022000`, below the authority journal.

- Device UI creation uses 64-sector / 32 KiB zero-write batches, reusing the
  existing 32 KiB worker diagnostic buffer. Previously six sectors / 3 KiB.
- No disk-format change. Default portable formatter remains six-sector compatible.
- Worker publishes successful-sector counts through atomic fields. Core 0 draws
  a progress bar, percentage, MiB written/total, average KiB/s and elapsed seconds.
- Preparation and finalization have separate screen labels. 100% metadata written
  is not reported as successful vault creation until sync/headers/authority finish.
- 24 desktop tests pass, including buffered-format boundaries, write/sync failures
  and monotonic progress. ASan/UBSan suite 24/24 passes (leak detector disabled).
- Progress framebuffer visually inspected; ARM firmware builds successfully.
- No attached-board writes or firmware updates performed. Existing running format
  cannot acquire progress instrumentation without a subsequent firmware update.
- Raw four-bit 25 MHz ceiling is 12.5 MB/s before overhead; 3.66 GiB takes over
  five minutes even at that ceiling. Actual improved speed awaits board measurement.
