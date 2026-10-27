Fuse Vault hardware-test firmware — 2026-10-19

Image: fuse-vault-d7c5206-full.uf2
Source: d7c5206c2e5944a9bc09af3b5cf2c7061f869c98 (latest local main).
Existing uncommitted changes are documentation only.
Board: fuse_vault, RP2350 ARM Secure / RP2354 custom hardware.
Release build: physical TFT and device UI, USB MSC, FIDO enabled.
All FV_DEBUG_* options disabled. This is development firmware, not a
production-security release. No device has been flashed by this task.

SHA-256:
790e18eeb60940c3820d8b3f78e7a53072228728e6b2ac7977b2f6671d1232b4

Install using the board's usual BOOTSEL procedure and copy this UF2 to its
BOOTSEL drive. Reconnect normally and use the physical screen/buttons.

Previously used sticks:
- Firmware flashing does not reset OTP. New enrollment skips every occupied
  token slot, including raw ECC bits; capacity is up to 228 on fresh OTP.
- Old root-based enrollment/journal formats are not migrated. Back up anything
  needed using the old firmware before testing. Explicit new setup replaces
  the old journal and creates/formats a fresh encrypted vault and FIDO store.
- With the old application allocation and an untouched page-3 mask, occupied
  legacy OTP is recognized and the UI permits fresh setup in a blank slot.
- Malformed masks, unreadable/locked OTP, or a READY token with an invalid
  journal fail closed. Do not erase the journal to bypass such an error.
- Physical OTP/ECC and power-cut validation remain pending; this image is
  prepared for that testing, not evidence that the particular stick passed.

Build and packaging:
cmake --build build-board-full -j4
v1/firmware/build/_deps/picotool/picotool uf2 convert \
  build-board-full/fuse_vault_security.elf \
  dist/firmware-20261019/fuse-vault-d7c5206-full.uf2 \
  --family rp2350-arm-s --platform rp2350

picotool verified the UF2 identity and address range:
fuse_vault_security, RP2350 ARM Secure, 0x10000000–0x10078138.
The image ends below the reserved journal at 0x101fe000.

Validation on 2026-10-19:
- Full Pico firmware build passed.
- Regular host/FIDO suite: 44/44 passed.
- Debug host/FIDO suite with assertions, ASan and UBSan: 44/44 passed.
  ASAN_OPTIONS=detect_leaks=0; no leak-check claim.
- No physical device testing performed in this task.
