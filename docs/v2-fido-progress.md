# V2 FIDO delivery ledger

This is the persistent implementation plan and handoff record. Read this file
before resuming FIDO work after compaction or in another task. Update the current
step, evidence, and next action when work stops; do not infer completion from a
previous conversation summary. V1 is a reference archive, not the V2 runtime.

## Agreed requirements (2026-09-20)

- Composite USB: encrypted disk and FIDO available together; development CDC.
- Use the existing device unlock, no separate host-entered FIDO PIN. Built-in UV
  tokens still use the relevant authenticatorClientPIN subcommands.
- Verification reuse is configurable (user request): timed/same-site default
  (first use within 30 seconds, maximum age 10 minutes, fresh verification on RP
  change) or whole unlocked session across sites. Physical approval always remains.
- Fresh physical approval for registration/login; Select approves, Back rejects.
  Held buttons, previous submit presses, cancellation and stale requests cannot approve.
- Include resident passkey list/delete and a local browser testing application.
- FIDO lives in reserved SD sectors 16–2063, never in MSC geometry. Same selected
  XTS cipher stack and sector HMAC as USB, with independently derived FIDO keys.
- External storage replay is explicitly outside the threat model. SD is a
  development stand-in for future soldered NAND. Encryption does not prevent
  replay on the same device: old snapshots may undo deletion/FIDO reset.
- No supported credential backup/export. Use constant-zero signature counters.
  Attempt accounting/destruction remain authoritative in internal flash/OTP.
- Crash consistency remains required: two authenticated snapshot banks, complete
  commit before success, no silently recreated store on corruption.
- Debug framebuffer viewer first; physical trusted display, secure boot/debug/RAM
  hardening and certification remain separate release gates.
- Preserve the active USB pipeline edits; do not revert or overwrite unrelated work.

## Formal stages

| ID | Stage | Status | Acceptance gate |
| --- | --- | --- | --- |
| F1 | Portable pico-fido engine | COMPLETE — portable baseline | V2-owned pinned sources, injected callbacks, built-in UV/no external PIN, ES256 resident/nonresident registration and assertion independently verified; persistence callback failures deny success; normal/sanitizer tests and Cortex-M33 compile/link evidence |
| F2 | Encrypted FIDO store | COMPLETE — portable store | Independent FIDO derivations, 64 KiB object image in two snapshot banks, explicit initialization, complete-state recovery under injected interruptions, USB data unchanged |
| F3 | Composite USB and device authorization | IMPLEMENTED — hardware acceptance pending | Standard HID alongside MSC, bounded queues and responsive keepalives/cancel under disk load, trusted unlock/reverification, fresh approval, full session invalidation |
| F4 | Local management and browser harness | NOT STARTED | Resident list/delete, loopback WebAuthn server verifies registration and login, account selection and negative cases covered |
| F5 | Hardware compatibility and fault acceptance | NOT STARTED | Linux Chromium/Firefox first, additional OS/browser matrix, power-cut/reconnect/mux tests, simultaneous file integrity and passkey tests, measured capacity/latency/stack/RAM |

Each stage progresses through implementation, automated validation, and any
hardware acceptance separately. Passing a simulated engine test does not prove
browser or physical-device compatibility. Do not mark a stage complete while
its acceptance gate is pending. F1 can be delivered independently; no automatic
flashing, enrollment, SD initialization or real-account registration is authorized
by starting engine work.

## Interfaces and implementation direction

F1 uses the archived pico-fido revision
`09d95a469b3ca1142bb04b505b49ab77eba6e964`, Pico Keys SDK
`263b2a9839acb3a16936199ca5dbb814e8bf16e1`, and TinyCBOR
`c0aad2fb2137a31b9845fbaae3653540c410f215` as the initial baseline. Keep
licenses/provenance; review upstream changes before hardware/release claims.
Expose one serialized engine with injected entropy, time, approval, UV, cancellation,
local authorization, and durable snapshot commit. Never bind simulated approval
callbacks into firmware. No upstream boot, USB, OTP or physical flash code.

F2 adds store open/commit/close around the existing V2 pipeline, deriving keys
from the unlocked VMK and volume identity. No per-commit internal anti-rollback
anchor. Credential rewrap preserves FIDO; vault destruction denies access to both.
FIDO-only reset invalidates wrapped and resident credentials, leaving USB intact.

F3 keeps USB/UI on core 0 and all keys/engine/SD on core 1, with bounded cooperative
work and deferred MSC completion. Add a trusted reverification API that preserves
an open disk after success; failed submitted credentials consume shared attempts
and lock the session. Invalidation clears tokens and pending approval generations.

F4 uses python-fido2 server verification with SQLite and plain browser WebAuthn at
http://localhost:8000, exact origin/RP checks and single-use challenges. Do not
substitute a browser virtual authenticator for tests of the actual engine/device.

## Validation matrix

- Engine: ES256 signatures, UV/UP flags, both PIN/UV token protocols, direct UV,
  resident discovery and allow lists, wrong RP, denial, token permissions, reset,
  persistence/reopen, full store, malformed messages, failed RNG/commit, cleanup.
- Store: wrong key, corruption, torn payload/metadata/commit, credential changes,
  allowed old-image replay, destruction authority, USB region boundaries.
- Integration: lock/eject/reset/suspend/disconnect/mux/media faults; held buttons,
  stale approval, cancellation and UI ownership; large disk transfers during FIDO.
- Browser: UV/resident/attestation options, multiple accounts, invalid origin,
  challenge replay, policy rejection. Custom devices may be rejected by attestation,
  AAGUID, platform-only or unsupported algorithm/extension policies.

## Current handoff

- **Current stage: F3, board discovery/registration/login/reconnect passed; load stability acceptance pending.**
  Build/runbook: `docs/v2-fido-testing.md`; evidence:
  `results/fido-integration-build-20260921.md`. Development UF2 is
  `build-pico-fido/fuse_vault_security.uf2`. User flashed the corrected image and
  reported successful real-board registration and login; the agent has not flashed it.
- F3.1 **done**: asynchronous HID, composite descriptors, discovery, worker queues,
  keepalives, cancel/resync and reset window tied to USB enumeration.
- F3.2 **done**: encrypted store attachment and explicit on-device initialization;
  real unlock/re-verification, persistent attempts, RP/age cache, approval/rejection,
  modal input generations and session invalidation. Existing disk session survives
  successful re-verification; wrong verification locks it.
- F3.3 **done**: simulated firmware/HID/SD client tests, transport/UV/UI/scheduling
  tests, sanitizer coverage, composite and FIDO-disabled ARM builds; real USB
  smoke client `tools/fido_smoke.py` (register/login/info).
- Hardware discovery follow-up: the initial flashed image enumerated as four
  interfaces and Linux granted adam read/write on hidraw3, but python-fido2
  rejected its HID descriptor. Fixed by repeating output report size; descriptor
  length is now derived from the shared array. Firmware adapter regression passes
  through the actual client descriptor parser. Corrected UF2 rebuilt and user confirmed discovery after reflashing. Smoke tool
  now accepts serials.
- F3.4 **partially passed**: user-provided output confirms discovery, registration
  signature verification and login signature/RP/UV/UP/zero-counter verification on
  board 66ED2A91873CF67F. User also confirmed login after reconnect and with the
  filesystem mounted. Active disk traffic caused debug timeouts and FIDO freezes;
  rejection/cancellation and concurrent-load acceptance remain;
  concurrent disk integrity and keepalive timing, physical held-button checks,
  suspend/mux/reset/SD faults, measured stack/heap. Complete the runbook acceptance
  before marking F3 complete; F4 remains the next implementation stage.
- Active-load stability fix: pinned TinyUSB `tud_task_ext` drained its event queue
  until empty while deferred MSC operations requeued themselves. This starved the
  outer-loop UI/CDC/FIDO polls, including the mailbox acknowledgement needed by
  the worker. A generated local `usbd_budget.c` now caps each pass at eight events;
  the SDK is not edited. `tools/test_usb_task_budget.py` reproduces starvation in
  the original task body and passes with the fixed body, including ASan/UBSan.
  Actual MSC driver overlap/durable completion tests pass for 4/16/32 KiB buffers.
  Both firmware variants build. Corrected UF2 rebuilt; user reflash/load retest
  remains pending. The reported eventual USB restart has not been independently
  diagnosed; the screenshot's HID I/O error was likely the user's unplug.
- Configurable verification implemented: Settings → FIDO Verification offers timed /
  same-site or whole unlocked session. Stored as a local-only versioned record in
  the encrypted engine snapshot; existing stores default to timed. Saving clears
  UV reuse and host tokens; next use re-verifies once. Wrong credentials and host
  cancellation retain their existing shared-session lock behavior. No automatic
  USB lock timer added. User must reflash the policy-capable UF2 and select mode.
  Full 33/33 normal CTests and 9/9 focused sanitizer tests pass; policy persistence,
  cross-site/long-age reuse, switching back, invalid/locked setters and failed save
  are covered by the firmware adapter peer. Both ARM firmware variants build.
- Current automated evidence covers 33 CTests across full and focused runs, normal
  and ASan/UBSan. The firmware adapter peer uses simulated UI/platform adapters;
  this is not a physical USB or browser test. RP display currently rejects IDs
  beyond 100 printable ASCII bytes. Production trusted TFT remains unavailable.
- F2 validation: 30 CTest cases validated normally and under ASan/UBSan across
  full-suite and focused runs. Includes 370 commit interruption cases, initialization
  cuts, dropped unsynchronized writes, stale owners, corruption/mixed-snapshot
  rejection, allowed replay, credential changes and destruction authority. The
  independent client verifies signatures after encrypted reopen and credential
  rewrap. See `results/fido-storage-build-20260920.md`.
- Cortex-M33 store compilation and engine/store standalone linking passed. Store
  write/open frames are 12,816/11,704 bytes; the engine and store
  are now linked in firmware with a 64 KiB worker stack reservation. Actual
  high-water/heap and runtime performance still need board measurement.
- The portable F1 baseline is implemented
  in `src/fido` and `third_party/pico_fido`, enabled by `FV_ENABLE_FIDO_ENGINE`.
  Build commands and callback/ownership contracts are in `src/fido/README.md`.
- Validation on 2026-09-20: 27/27 V2 CTests with FIDO enabled, 27/27 under ASan/UBSan
  (`detect_leaks=0`), 25/25 with FIDO disabled. Independent python-fido2 verifies
  direct UV, both token protocols, real signatures, resident/nonresident keys,
  scope/permission/expiry errors, presence denial, close/reopen, deletion/reset,
  zero counters/no counter writes, failed RNG/commit, cancellation, malformed
  CBOR and full-store recovery. See `results/fido-engine-build-20260920.md`.
- Cortex-M33 library compilation and standalone link passed. These are not a
  Pico firmware memory-layout or hardware runtime check. Largest engine frame
  is 6,696 bytes; nested stack/heap budget needs F3 validation.
- F2 now authenticates recovered images before engine open; initialization is a
  separate explicitly destructive API, never automatic recovery. It retains no
  derived keys in its handle and checks active internal authority. Engine/store
  close and plaintext wiping must still be bound to every firmware invalidation.
  Both RAM and encrypted file-backed peers use simulated UV/presence/authority;
  these test callbacks must never become firmware authorization callbacks.
- F3 now ties reset to enumeration and integrates the RP-bound UI/runtime policy.
  The first implementation is testable; hardware acceptance remains explicit.
- Full upstream security-diff review remains before hardware/release readiness.
  Release notes were inspected, but the pinned upstream commit was not retrievable
  through the web reader. This imported baseline is not a security audit.
- The worktree is shared with USB pipeline work; another writer committed some
  in-progress F1 files during this task. No commits or reversions were made here.
- No flashing, OTP changes, physical SD writes, browser trials or hardware tests performed.

## Estimates

F1 3–5 days; F2 4–6; F3 6–10; F4 3–5; F5 4–9 (20–35 engineering days total).
These are planning estimates, excluding physical TFT repair and production security
hardening/certification. Update estimates based on measured integration findings.
