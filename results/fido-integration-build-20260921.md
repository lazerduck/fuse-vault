# F3 development integration evidence — 2026-09-21

Implemented an opt-in composite FIDO HID/MSC/CDC adapter, worker-owned engine and
encrypted store, atomic modal mailbox, device re-verification, RP/freshness cache,
explicit local store initialization and asynchronous HID cancellation/keepalives.
Added real USB test client and extended the framebuffer viewer with request-bound
input generations. No hardware was flashed or accessed for this evidence.

Validation:

- Full desktop suite: **32/32 passed**, normal and ASan/UBSan
  (`ASAN_OPTIONS=detect_leaks=0`). After adding the scheduling test, focused runs
  cover all **33** current cases across full and focused evidence. Latest adapter,
  transport, UI and scheduling changes pass the sanitizer rerun.
- Actual firmware adapter compiled in a desktop peer with simulated platform/UI,
  real engine, HID framing and encrypted file-backed storage. Independent
  python-fido2 verifies registration and signed assertions, encrypted reopen,
  first unlock/RP change/expiry, physical rejection, cancellation, wrong-secret
  shared attempt accounting, UV token use, malformed RP rejection, expired reset
  rejection after engine reopen, and late cancellation after worker completion.
- HID tests cover fragmentation, sequence error, receive timeout, pending-buffer
  ownership, busy, cancellation, INIT resynchronization and output backpressure.
  UV tests cover exact first/max age boundaries, RP binding and clock wrap.
- UI tests cover explicit destructive initialization confirmation, approve/reject,
  fresh modal state and credential wiping. Physical debounce and real inter-core
  races still require hardware acceptance.
- USB bridge tests cover batch deferral during crypto, disk service during prompt
  windows, cached status/sync and bus invalidation deferred until worker completion.
- Re-verification test uses the actual vault lifecycle, preserves the VMK and
  generation on success, charges one wrong attempt and denies a locked session.
- Cortex-M33 composite firmware and existing FIDO-disabled device UI firmware
  both compile/link successfully. The smoke client help and Python compilation
  checks pass. UF2 packaging succeeded without flashing.

Build command: see `docs/v2-fido-testing.md`. The current composite ELF reports
413,764 bytes text, 346,424 bytes BSS, zero separate data in `arm-none-eabi-size`.
This is not a peak heap/stack measurement. The worker stack reservation is 65,536
bytes; compiler frames include re-verification at 7,624 bytes and FIDO dispatch
at 216 bytes, in addition to nested engine/store/crypto frames.

UF2: `build-pico-fido/fuse_vault_security.uf2`
SHA-256: `1df28ec57a80391bb3cd7eb560af607d4eb92a8dd5affbe1c2d94bc23ece129e`

F3 software is ready for initial development board tests. Its physical acceptance
is still pending, so the formal stage remains in progress. No browser/site,
physical TFT, real dual-core timing, power-cut, SD load, stack high-water or
certification result is implied by these tests.

## Hardware discovery follow-up

After the user flashed the first build, read-only host inspection confirmed four
USB interfaces, HID interface 3, and an ACL granting adam read/write access to
`/dev/hidraw3`. Linux identified it as a FIDO token. The installed python-fido2
parser nevertheless raised `ValueError("Not a FIDO device")`: it clears report
size/count after INPUT and requires both to be repeated for OUTPUT. The original
27-byte descriptor inherited size according to HID semantics; the corrected
29-byte descriptor states both explicitly. The USB descriptor length now comes
from the same shared array. The firmware adapter regression now parses the actual
callback bytes with python-fido2 and passes, and the corrected ARM image builds.
The hash above identifies the corrected UF2. No flashing was performed by the
agent; discovery on the corrected flashed board is still to be confirmed.

## User-confirmed board smoke test

The user subsequently reported successful GetInfo, registration and login using
board `66ED2A91873CF67F` and the corrected image. The smoke client printed
“Registration verified” and “Login signature, RP, UV/UP flags and zero counter
verified.” Keepalives progressed through PROCESSING / UPNEEDED. This is evidence
of successful hardware registration and assertion, not yet reconnect persistence,
concurrent disk integrity, cancellation/fault acceptance or browser compatibility.

## Active-load scheduling correction

User subsequently confirmed reconnect persistence and login with the filesystem
mounted, but reported freezes under active disk traffic. The visible HID I/O error
was likely caused by unplugging during an operation, per the user's clarification.

Inspection found that the SDK's no-OS `tud_task_ext` drains the event queue until
empty. Deferred MSC callbacks enqueue retries, so it can indefinitely starve
outer-loop UI/CDC/FIDO polling. The pinned generated USBD copy now handles at most
eight events per call. Test `tools/test_usb_task_budget.py` compiles the exact task
function body against a retrying event source whose completion requires application
progress. The original fails deterministically with starvation; the patched version
returns to UI/CDC/FIDO polls and completes. Normal and ASan/UBSan runs pass.
Sanitizer execution uses `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot
run under this environment's tracing. The actual MSC BOT driver overlap/durable
completion suite also passes under ASan/UBSan for 4096/16384/32768-byte buffers.

Composite FIDO and FIDO-disabled UI firmware both rebuild successfully. The hash
above now identifies the scheduling-corrected UF2. Hardware load retesting remains
pending; the eventual USB restart was not independently reproduced or diagnosed.

## Configurable verification follow-up

Added persistent on-device timed/same-site and unlocked-session modes. Defaults
remain unchanged for existing stores. No separate USB auto-lock timer added. The
firmware peer verifies encrypted reopen preserves the setting, session reuse across
RP/time changes still requires presence, switching to strict restores re-verification,
locked/invalid setters are rejected, and injected save failure is not reported as
success. Save clears cached UV and host tokens. Full normal suite: 33/33 passed;
focused FIDO/UI ASan/UBSan suite: 9/9 passed (detect_leaks=0), followed by normal
and sanitized failed-save regression runs. Composite and FIDO-disabled UI ARM
builds passed. Hash above identifies the latest policy-capable UF2. On-board policy
selection/persistence testing is pending; no device was flashed by the agent.
