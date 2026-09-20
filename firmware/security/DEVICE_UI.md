> **Current format:** new volumes use the [lazy metadata bitmap](../../docs/v2-lazy-metadata.md).
> Setup clears about 938 KiB on the current card instead of 3.66 GiB. The eager
> initialization formula/timings below describe the earlier layout. Existing
> volumes remain supported; flashing alone does not convert them.

# V2 device setup and USB screen

## What this build provides

- Debounced USB selection: C whenever C has power; A only when A alone has power.
  With neither supply sensed, both data routes are disconnected. Route changes
  disconnect USB, disable the mux, change SEL and reconnect after 25 ms. Every
  route change invalidates the storage session. GPIO2/3 are the canonical senses;
  duplicated GPIO16/17 stay inputs, without pulls.
- The firmware owns a 160×80 monochrome framebuffer and the UI state machine.
  Physical active-low buttons on GPIO10–15 and development USB key injection feed
  the same handler. The laptop renders the actual framebuffer; it has no separate
  setup implementation. The disconnected TFT has no SPI driver in this build.
- Setup selects a confirmed direction pattern, four 00–99 code wheels, or four
  words through a directional picker, plus 1–4 ordered AES-256-XTS or
  Camellia-256-XTS layers, and a failure policy. Repeated ciphers are allowed;
  existing derivations give each layer separate keys. Credential profile 2 uses
  bytes up=1, down=2, left=3, right=4; Select submits and Back deletes/cancels.
- Policy: 1–100 failed attempts, default 10; destroy the vault key (default) or
  permanent lockout. These are the existing persistent enforcement mechanisms,
  not a timeout. Settings require fresh credential verification, rewrap the same
  VMK, and leave the vault locked. Incorrect verification consumes real attempts.
- Full-card creation uses the existing authenticated volume format. Headers occupy
  sectors 0–15; sectors 16–2063 remain reserved for future secure objects/FIDO;
  metadata starts at 2064. Maximum logical sectors satisfy
  `2064 + ceil(logical / 15) + logical <= physical sectors`, capped at UINT32_MAX
  for USB READ(10). Approximately 6.25% goes to tags, plus the fixed reservation.
  The reserved area is not yet an implemented FIDO object store.

## Build

```sh
cmake -S firmware/security -B build-pico-device-ui \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release -DFV_USB_MSC=ON -DFV_DEVICE_UI=ON \
  -DFV_DEBUG_SCREEN=ON -DFV_DEBUG_SESSION=ON \
  -DFV_DEBUG_ENROLLMENT=OFF -DFV_DEBUG_OTP_INSPECT=OFF \
  -DFV_DEBUG_STARTUP=OFF -DFV_DEBUG_BOOT_TRACE=OFF
cmake --build build-pico-device-ui -j4
/tmp/fv-picotool-usb/picotool uf2 convert \
  build-pico-device-ui/fuse_vault_security.elf \
  build-pico-device-ui/v2_device_ui.uf2 --family rp2350-arm-s --platform rp2350
```

Use the working custom board, `66ED2A91873CF67F`. Flashing preserves the existing
vault and journal. It does not provision OTP or reformat SD automatically.

## Existing 1 MiB sample: move to full-card setup

1. Flash `build-pico-device-ui/v2_device_ui.uf2`, reconnect normally.
2. Before opening the viewer, unlock the existing public-credential test vault:

   ```sh
   python3 /home/adam/projects/fuse-vault/tools/storage_session.py --device 66ED2A91873CF67F unlock
   ```

3. Start the viewer:

   ```sh
   python3 /home/adam/projects/fuse-vault/tools/device_ui.py --device 66ED2A91873CF67F
   ```

4. Unmount any vault filesystem. On the device screen choose **Erase and set up**,
   then explicitly confirm **Destroy**. This makes the old files inaccessible and
   revokes the current OTP token. Subsequent creation uses the next token slot;
   there are eight slots in this allocation, so this is not an unlimited reset.
5. Select setup, choose an unlock method, and enter and repeat the credential. On the cipher screen,
   Up/Down selects a row, Select/Right toggles its cipher or adds a layer, and Left
   removes the last layer. Choose Continue. Select failure policy and confirm.
6. Wait for metadata initialization to finish; keep power connected. Large cards
   can take minutes. The device stays locked afterward. Select Unlock and enter
   the new credential.
7. The host now sees the larger **unformatted** logical disk. Use the partition
   manager to create a filesystem on that device after confirming its identity.
   Formatting and normal file writes pass through the crypto/authentication path.

On a fresh or EMPTY enrollment, start at setup directly; successful confirmation
provisions fixed application OTP if needed. No raw OTP editor is exposed. Provisioning
or storage errors remain errors; corrupt authority never silently becomes EMPTY.
An existing active volume is never silently resized or reformatted.

The debug viewer requires Python3 + GTK3/PyGObject + Cairo (already available on
this laptop). Arrow keys send directions, Enter selects, Backspace/Escape goes
back. Physical board controls work too. The viewer rejects Select while the vault
is mounted in its Linux mount namespace; physical controls cannot inspect host
mounts. Always unmount before lock, settings changes, erase or changing USB routes.
Only one serial tool can hold the port at a time.

The public `storage_session.py unlock` command is only for the old test credential;
**do not use it on the new sequence-protected vault**. It would be a wrong attempt.
The viewer is the normal unlock path once setup is complete.

## Debug protocol and ownership

`SCREEN` returns JSON `{command:"screen",ok:true,width:160,height:80,
format:"mono-msb",screen:<enum>,busy:<bool>,pixels:<hex>}`. Pixels are 1600 bytes,
row-major, most significant bit first; 1 is foreground. `KEY UP|DOWN|LEFT|RIGHT|
SELECT|BACK` returns `{command:"key",ok:true,accepted:<bool>}`. Busy input is rejected,
not queued for later. Frames intentionally show entered arrows, code-wheel values
and selected words. Debug framebuffer access therefore exposes entered credentials;
it must be disabled for release. No VMK or derived encryption keys are returned.

```sh
python3 /home/adam/projects/fuse-vault/tools/device_ui.py \
  --device 66ED2A91873CF67F --snapshot /tmp/vault-screen.pbm
```

Core 0 owns UI state, framebuffer, buttons, mux and USB. Core 1 exclusively owns
keys, SD, OTP and flash. A single in-flight mailbox transports setup requests;
the ordinary queue contains only its pointer, so ring-buffer slots do not retain
credentials. Both mailbox and worker copy are wiped. Core 0 wipes its input after
dispatch, cancellation or disconnect. A job already accepted can finish after
disconnection; deferred invalidation locks it before storage can be exposed again.

During a UI operation, MSC returns not-ready without waiting behind a long metadata
format, while CDC screen/button handling remains responsive. USB invalidation is
retained until the worker is available. Screen updates use no shared worker reply
buffer while a diagnostic CDC command is outstanding. MSC now uses a
[two-buffer 32 KiB pipeline](../../docs/v2-usb-pipeline.md), overlapping USB with
worker encryption/authentication and SD work.

## Release boundary

`FV_DEBUG_SCREEN=OFF` removes screen extraction and remote button injection.
`FV_DEBUG_SESSION=OFF` rejects the public fixture unlocks. The normal device UI can
build with both disabled. This development artifact enables both for migration.
It is not production hardened: complete the production checklist, physical display
integration, protected execution/boot/debug policy and FIDO integration separately.

Host tests cover capacity maximality, setup/confirmation/cancellation, four layers,
policy bounds, permanent-lockout UI, and deferred USB invalidation during setup.
Hardware acceptance still needs full-capacity create/format/file round-trip,
cold reconnect/unlock, settings persistence and both mux routes/both-powered cases.

### Initialization progress update

`v2_device_ui_progress.uf2` adds measured metadata progress: percentage, MiB
written/total, average KiB/s and elapsed seconds. It uses 32 KiB writes through the
existing worker buffer instead of 3 KiB writes. The format is unchanged. Preparation
and finalization are labelled separately; a full bar means metadata writes finished,
not that the final vault commit has succeeded. The current in-flight format cannot
be updated live; let it finish before installing the new firmware.

### Flip display and controls

On the home screen (locked before credential entry, unlocked, or initial setup),
press Left or Right to flip the screen 180 degrees. The on-screen hint names this
shortcut. Up/Down and Left/Right physical inputs rotate together with the pixels;
Select and Back keep their roles. Credential direction values remain relative to
how the device is being viewed, so rotating the board does not change the secret.
Flip is unavailable during credential entry or an active operation.

The orientation is a RAM-only preference: retained across lock/unlock, UI refresh,
and USB route changes while powered, reset on power-on. It causes no storage writes
or lock/unlock actions. The debug viewer mirrors the actual rotated framebuffer;
its injected keys name the original physical board buttons and use the same
remapping as GPIO input. No viewer update or vault reformat is needed.

Artifact: `build-pico-device-ui/v2_device_ui_flip.uf2`.


### Unlock methods and changing credentials

`v2_device_ui_methods.uf2` offers three methods at setup:

- **Direction pattern:** 8–64 directions, shown as arrows. Back deletes one input.
- **Four code wheels:** four values from 00 to 99. Up/Down changes the selected
  value (wraparound); Left/Right chooses a wheel; Select submits all four.
- **Four words:** the stable 64-word V1 list, with three four-way choices per word.
  Arrows choose a range/subrange/word. Back undoes a choice or removes the last
  word. After four words, Select submits; an arrow restarts entry from that word
  position and clears later words. These are local credential words, not a BIP39
  recovery phrase.

Existing volumes automatically show the method stored in their flash-anchored
header. Reading that method does not attempt unlock or decrement the attempt
budget. Unsupported/corrupt headers fail without silently selecting another method.

On the unlocked home screen, unmount the filesystem and choose **Change unlock
method**. Enter the current credential, select a method (the same method can be
chosen to change its credential), enter the new credential twice, then confirm.
Final confirmation verifies the old credential under the normal attempt budget
and rewraps the same VMK. Files, encryption stack, failure policy and OTP token slot
remain unchanged. It finishes locked, requiring the new credential. A failed old
credential spends an attempt; cancellation before submission does not.

Only erase/recreate or final-attempt destruction followed by setup advances the
OTP token slot. Retry from an existing EMPTY enrollment reuses its slot. An
interrupted OTP provisioning write can strand a partially programmed slot.
There are eight token slots in the current fixed development allocation.
