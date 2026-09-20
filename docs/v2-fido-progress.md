# V2 FIDO delivery ledger

This is the persistent implementation plan and handoff record. Read this file
before resuming FIDO work after compaction or in another task. Update the current
step, evidence, and next action when work stops; do not infer completion from a
previous conversation summary. V1 is a reference archive, not the V2 runtime.

## Agreed requirements (2026-09-20)

- Composite USB: encrypted disk and FIDO available together; development CDC.
- Use the existing device unlock, no separate host-entered FIDO PIN. Built-in UV
  tokens still use the relevant authenticatorClientPIN subcommands.
- Verification reuse: first use within 30 seconds, completion within 10 minutes,
  bound to the first RP; fresh local verification on expiry or RP change.
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
| F1 | Portable pico-fido engine | IN PROGRESS | V2-owned pinned sources, injected callbacks, built-in UV/no external PIN, ES256 resident/nonresident registration and assertion independently verified; persistence callback failures deny success; normal/sanitizer tests and target compile evidence |
| F2 | Encrypted FIDO store | NOT STARTED | Independent FIDO derivations, 64 KiB object image in two snapshot banks, explicit initialization, complete-state recovery under injected interruptions, USB data unchanged |
| F3 | Composite USB and device authorization | NOT STARTED | Standard HID alongside MSC, bounded queues and responsive keepalives/cancel under disk load, trusted unlock/reverification, fresh approval, full session invalidation |
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

- F1 started. Existing V1 engine/client examined; V2 has no FIDO target yet.
- Unrelated USB pipeline changes are already present in the worktree.
- Next: create a standalone V2 engine target and built-in-UV host fixture, enforce
  zero counters, then run independent python-fido2 and existing V2 regressions.
- No hardware changes or tests performed for this milestone yet.

## Estimates

F1 3–5 days; F2 4–6; F3 6–10; F4 3–5; F5 4–9 (20–35 engineering days total).
These are planning estimates, excluding physical TFT repair and production security
hardening/certification. Update estimates based on measured integration findings.
