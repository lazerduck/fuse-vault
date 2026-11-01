# Unlock modes — 31 October 2026

Implemented on the active `src/` firmware, based on commit
`196cfe503c22b49f2de90c04af51de596cd71a1f` plus the local changes in this workspace.
No board was flashed or provisioned by this task.

## Behaviour

| Locked-screen input | Mode | Host files | FIDO |
| --- | --- | --- | --- |
| Select | Full access | Read/write | Enabled |
| Up | Passkeys only | No medium | Enabled |
| Down | Files read only | Read only | Private operations disabled |

The same credential and persistent attempt budget apply to all modes. Switching
mode requires locking and authenticating again. Directions follow screen
orientation; the selection press must be released before credential input.
The credential screen and dashboard show the selected scope. Settings expose
only the operations permitted in that scope.

Host-triggered FIDO unlock grants passkeys-only access. Reverification preserves
scope. Read-only FIDO requests cannot prompt, open the private store, charge a
credential attempt or broaden access. FIDO requests cannot replace a local
credential/settings flow. FIDO HID remains enumerated; USB discovery is public.

Read-only USB media advertises the MODE SENSE(6) write-protect bit and denies
WRITE(10) with DATA PROTECT / WRITE PROTECTED (27/00). The worker enforces access
independently of USB reporting. Commands/batches carry a session generation;
transport completion also checks the worker's currently published generation.
The generated TinyUSB patch preserves application sense on late failures.

No SD format, cipher stack, credential encoding, OTP layout or enrollment
migration is introduced. Read-only is a restriction on host file-volume writes;
authentication state and existing redundant-header repair still operate.

## Local validation

- Full RP2354 firmware build passed with UI/TFT/MSC/FIDO and all debug switches
  off. Picotool verified the resulting UF2 identity and image bounds.
- The final affected-test run passed 10/10 under AddressSanitizer and
  UndefinedBehaviorSanitizer: vault lifecycle/access scope, hardware-SHA contract,
  UI, USB callbacks, worker batches, invalidation and FIDO scheduling/adapter.
- The actual generated TinyUSB MSC driver passed under ASan/UBSan with 4,096,
  16,384, 32,768 and 65,024-byte buffers. Coverage includes the MODE SENSE WP bit,
  rejected WRITE/REQUEST SENSE 27/00, late errors and old ready-read suppression.
- FIDO adapter tests cover scope-preserving verification, no private I/O or
  prompts in read-only mode, local-entry races, cancellation versus USB
  invalidation, lock before response publication and lock between HID fragments.
- UI tests exercise all three credential methods and three modes in both screen
  orientations. Offline previews were rendered and visually checked.
- `git diff --check` passed. Leak detection was disabled for the sanitizer runs
  (`ASAN_OPTIONS=detect_leaks=0`); no leak-check claim is made.

All 45 distinct host/FIDO tests passed across the broad run and final targeted
reruns. The exhaustive FIDO interruption case passed in 892.80 seconds; the
512-credential/reference case passed in 349.21 seconds. The broad run initially
recorded 44/45 because the adapter test used a peer binary built before its new
test command was added. Rebuilding that peer and rerunning the affected tests
resolved the harness mismatch. The final affected-test run was 10/10.

Firmware SHA-256:

```text
5dbbceedf20a90992596fbee55fea5e4bcfab9e487b8c3b9461271eb67908c40
```

Image range: `0x10000000–0x1007ced0`; the reserved authority journal begins at
`0x101fe000`. ELF section totals: text 517,836 bytes, data 0, BSS 368,072 bytes.
These are build-time sizes, not a hardware measurement of peak stack/heap use.

Reproduction commands (from the repository root):

```sh
cmake -S . -B build-unlock-modes-host \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DCMAKE_BUILD_TYPE=Debug -DFV_SANITIZERS=ON
cmake --build build-unlock-modes-host -j8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-unlock-modes-host --output-on-failure -j4
cmake -S src/pico/device -B build-unlock-modes-board \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DFV_DEVICE_UI=ON -DFV_USB_MSC=ON -DFV_USB_FIDO=ON \
  -DFV_TFT_DISPLAY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-unlock-modes-board -j8
ASAN_OPTIONS=detect_leaks=0 python3 src/host/tools/test_usb_pipeline_driver.py \
  --sdk /home/adam/pico-sdk --firmware-build build-unlock-modes-board --sanitize
v1/firmware/build/_deps/picotool/picotool uf2 convert \
  build-unlock-modes-board/fuse_vault_security.elf \
  dist/firmware-20261031-unlock-modes/fuse-vault-unlock-modes.uf2 \
  --family rp2350-arm-s --platform rp2350
```

## Board acceptance after flashing

Use the existing BOOTSEL procedure and copy the packaged UF2. The image uses the
repository's existing `fuse_vault` RP2354 board/pin configuration, physical TFT,
MSC and FIDO, 32 KiB USB buffers, with all `FV_DEBUG_*` switches disabled. These
settings do not constitute production OTP/secure-boot hardening.

Existing current-format vaults should unlock without setup or re-enrollment.
This change does not add migration for older, already-incompatible development
formats. Keep the old image available for comparison; do not choose erase/setup
as part of testing this update on an existing vault.

1. Select: unlock, read/write a test file, and register/use a test passkey.
2. Eject and lock. Up: unlock and use a passkey; confirm no file volume mounts.
3. Lock. Down: unlock and read the file. Confirm the OS reports read-only media
   and rejects creating, editing, deleting and renaming files. FIDO must not prompt
   or authenticate, and its requests must not lock the readable file session.
4. Eject/lock and return to Full access; confirm the host refreshes writeability
   and can write again. Repeat on each supported OS where available.
5. Confirm the mode labels, flipped Up/Down mapping, and that holding the mode
   selection button does not add a credential input or approve FIDO.
6. Reconnect, suspend/resume and change USB ports: access must return to locked.
   Start a website login while locked: the credential prompt must say Passkeys
   only, and successful login must not mount files.

Physical board, OS mount/cache behaviour and power-interruption acceptance remain
the deployment checks; desktop simulation cannot establish those results.
