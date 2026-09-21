# F3 development USB testing

F3 now supplies a composite CDC + encrypted MSC + FIDO2 HID firmware image and a
real USB registration/login smoke test. This is sufficient to start board tests.
Basic physical USB discovery, registration, login, reconnect persistence and login
with a mounted filesystem have passed in user testing. Active disk-load stability
and browser acceptance remain.
F4 will add resident credential management and a localhost WebAuthn application.

## Build

From the repository root:

```sh
cmake -S firmware/security -B build-pico-fido \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release -DFV_USB_MSC=ON -DFV_DEVICE_UI=ON \
  -DFV_USB_FIDO=ON -DFV_DEBUG_SCREEN=ON -DFV_DEBUG_SESSION=OFF \
  -DFV_DEBUG_ENROLLMENT=OFF -DFV_DEBUG_OTP_INSPECT=OFF
cmake --build build-pico-fido -j4
```

With SDK picotool enabled, the normal extra-output target produces UF2. For the
local no-picotool configuration, convert the ELF with a separately built picotool:

```sh
picotool uf2 convert build-pico-fido/fuse_vault_security.elf \
  build-pico-fido/fuse_vault_security.uf2 --family rp2350-arm-s --platform rp2350
```

The development artifact is `build-pico-fido/fuse_vault_security.uf2`. Building or
converting it does not flash a board. Flash/enrollment/physical SD initialization
were not performed as part of this implementation. Preserve the board's existing
persistent authority allocation when using the project's normal firmware update
procedure; do not erase the whole flash or reprovision OTP to test FIDO.

FIDO is opt-in; ordinary security firmware still builds without it. Enabling it
requires MSC and device UI and forbids the public-fixture debug unlock interface.
USB uses the existing `cafe:4022` identity, revision `0300`, HID interface 3,
endpoints OUT 04 / IN 84, the FIDO usage page and 64-byte reports. The development
USB identity is not a certified/product-assigned authenticator identity.

## First test on the board

1. Use an enrolled test vault. Open the development framebuffer viewer:
   `python3 tools/device_ui.py --device BOARD_SERIAL`.
2. Unlock through the device's configured pattern/wheels/words. With the disk
   unmounted, choose **Settings → Initialize FIDO → Initialize**. This explicitly
   erases the reserved passkey store, preserving the USB data region. Do it once
   for an uninitialized test device, not before each login. Missing/corrupt stores
   otherwise fail closed; a host request never initializes them automatically.
3. Check discovery with `python3 tools/fido_smoke.py info`.
4. Register using `python3 tools/fido_smoke.py register`. Verify the displayed
   `REGISTER …fuse-vault.test` RP, then Select to approve or Back to reject.
5. Run `python3 tools/fido_smoke.py login`. Verify the displayed RP and approve.
6. Safely unmount/eject, unplug/reconnect, then repeat `login`. Unlock/re-verify
   when prompted. The saved public credential record must still verify the
   assertion against the same public key.

The smoke tool requires `python-fido2` and access to the FIDO hidraw interface.
The viewer uses the project's existing GTK/pyserial dependencies. If discovery
finds no accessible device, check USB enumeration and the user's hidraw access;
do not solve this by routinely running the viewer/client as root. With several
boards, select the serial with `--device BOARD_SERIAL` or the exact hidraw path
with `--device /dev/hidrawN`.

The initial F3 image had a HID descriptor interoperability bug: Linux recognized
the FIDO interface and granted access, but python-fido2 rejected the descriptor
because output report size was not repeated. Reflash the corrected UF2 if this
occurs; adding permissions will not fix that version. The firmware descriptor is
now checked by the installed python-fido2 parser in the adapter regression test.

The client creates one resident credential under a unique reserved `.test` RP.
`fido-test-credential.json` contains the RP, credential ID and public key, never a
private key or VMK. Keep this file to repeat login after reconnect. Choose another
`--credential PATH` for another test; the tool refuses to overwrite an existing
record. Deleting the host file does not delete the device passkey. Local individual
credential deletion arrives in F4; **Initialize FIDO** removes all test passkeys.
The tool verifies ES256 signatures, RP hash, UV/UP flags, and zero counters. It
never sends an unlock secret, auto-approves, resets, or initializes the device.
Ctrl-C sends CTAPHID cancellation through python-fido2. Requests also have a
120-second client timer and each device prompt has a 60-second timeout.

The debug viewer permits Select with the filesystem mounted only inside a FIDO
modal. Settings/lock actions retain its unmount guard. Injected keys carry a modal
generation; stale queued keys cannot approve the next prompt. Physical input
requires all buttons to be released after a new modal before a fresh press.
Debug keys and screen export are development features, not a trusted production
verification/display boundary.

## Integration behavior and limitations

- Core 0 owns TinyUSB, HID framing, screen and inputs. Core 1 exclusively owns
  engine, SD, VMK and derived keys. Mailboxes transfer ownership atomically.
- Initial verification reuse is under 30 seconds, total reuse under 10 minutes,
  bound to the first RP. Other RP/expiry asks for the device credential again.
  Every registration/assertion requests presence approval. A submitted wrong
  credential charges the shared persistent attempt policy and locks the session.
- Re-verification preserves the existing disk session on success. Lock, USB
  invalidation and failed verification clear engine/UV state. Cancellation drains
  the in-flight worker before HID buffers are reused. Late cancellation also
  locks the completed session; committed writes may have
  completed before cancellation. Missing media is checked before dispatch, and
  media/authentication I/O failures lock the shared session.
- HID keepalives target 50 ms from core 0. Disk batches are deferred while core 1
  is doing FIDO crypto/commit; during human prompts the worker pumps disk jobs.
  MSC status uses its last worker snapshot during FIDO; sync is already durable
  per completed write. Sustained latency/keepalive timing needs measurement on
  hardware, especially with four cipher layers and slow/erroring cards.
- Host FIDO reset is limited to the first 10 seconds after USB enumeration and
  requires approval; reopening the engine does not restart that window. Device
  **Initialize FIDO** remains an explicitly destructive local alternative.
- The approval screen shows the operation and host-supplied RP ID. It does not
  authenticate the browser origin independently. This first display accepts only
  printable ASCII RP IDs up to 100 bytes; longer/non-ASCII IDs are rejected rather
  than silently truncated. Account selection/display improvements belong to F4.
- No client PIN, CTAP1/U2F, vendor extensions or external attestation certificate
  chain. This is the existing ES256/self-attestation engine profile, not a promise
  of compatibility with sites requiring approved hardware, enterprise attestation,
  a particular AAGUID, platform authenticators or other algorithms/extensions.
- Accepted SD replay remains unchanged. Restoring an old image on the same device
  can undo FIDO deletion/reset. Encryption and future soldered storage do not
  establish snapshot freshness. Signature counters remain zero.

## Hardware acceptance still required

Record results separately from the automated tests:

- Reject registration/login; time out; Ctrl-C; hold Select from the prior screen;
  queue debug keys; confirm that none causes an unintended approval.
- Check RP change and both freshness deadlines; wrong submitted secrets consume
  exactly one real attempt. Use a disposable test enrollment with known remaining
  attempts for this check.
- Disconnect, bus reset, suspend, mux switch and eject during secret entry,
  approval, signing and snapshot commit. No stale successful response or reusable
  authorization may survive; reconnect must recover a complete store.
- Copy and hash a test file during registration and signing. Verify disk integrity
  and responsiveness, including keepalive gaps, cancellation latency, slow/erroring
  cards and four-layer encryption. Measure worker stack high-water and heap.
- Check Linux browser behavior in F4; certification/site policy and additional
  OS/browser acceptance remain F5. Upstream security-diff review and production
  debug/display/boot hardening are still outstanding release gates.

## Active disk-load freeze correction

The user reported debug-viewer timeouts and a frozen login while the disk was busy,
followed by an apparent USB restart. The initial TinyUSB task drained its event
queue until empty; deferred MSC callbacks kept adding retry events. Consequently
the outer loop could stop servicing CDC, FIDO keepalives and UI mailboxes. The
worker could then wait forever for a UI acknowledgement. This is a reproduced
software starvation path; it does not establish the cause of every USB restart.

The latest UF2 includes a pinned local USBD patch that limits each task pass to
eight events. No SDK files were changed. Regression commands:

```sh
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_usb_task_budget.py \
  --sdk /home/adam/pico-sdk --firmware-build build-pico-fido --sanitize
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_usb_pipeline_driver.py \
  --sdk /home/adam/pico-sdk --firmware-build build-pico-fido --sanitize
```

After reflashing, keep the viewer open and repeat login during a sustained read
from the mounted vault, including a login that requests fresh verification. Watch
for continuing screen updates and keepalives. Then test copying a disposable file
and comparing its hash. Do not initialize FIDO again; use the existing test record.
These hardware retests are still pending.

## Configurable FIDO verification

After installing the policy-capable firmware, unlock and use **Settings → FIDO
Verification**. Up/Down selects a mode, Select saves it, and Back cancels. The
viewer retains the existing unmount guard for settings: unmount the filesystem
before changing this setting, then mount it again for normal use. Saving does not
lock the vault or change its encryption/credential. Do not initialize FIDO again.

- **Timed / same site** (default): first reuse within 30 seconds, total age under
  10 minutes, bound to the first RP. Expiry/site change asks for the credential.
- **While vault is unlocked**: no reuse time limit or RP-change re-entry. A valid
  device unlock can verify FIDO operations across sites until the session is
  invalidated. Every registration/assertion still requires explicit approval.

The choice persists as a versioned local-only record in the authenticated encrypted
FIDO snapshot. Old stores with no record retain the timed default. Host CTAP
commands cannot set this policy. An invalid encoding fails closed. Save clears
verification and host authorization tokens, so the next FIDO use requires one
fresh credential entry. FIDO initialization clears the setting along with passkeys;
ordinary FIDO reset preserves this device preference. Accepted SD rollback also
applies to this setting.

These modes govern FIDO credential re-entry, not USB disk auto-lock. Physical
approval remains necessary in both modes. Existing wrong-credential and host
cancellation behavior still locks the shared session; this change does not make
those operations safe during filesystem writes. An unlocked unattended device
using session mode can authorize another site if someone operates its buttons.
