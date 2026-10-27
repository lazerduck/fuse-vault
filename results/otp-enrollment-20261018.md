# Token-only OTP enrollment — merged 2026-10-18

[PR #1](https://github.com/lazerduck/fuse-vault/pull/1) merged into `main` on
2026-10-18 at 07:02:55 UTC as `d7c5206c2e5944a9bc09af3b5cf2c7061f869c98`.
The validated PR head was `6e61fca96b30d7100f34b9985fa11242ba65b51c`.
This completes the software work for [improvement 1](../docs/Improvements.md#1-expand-enrollment-capacity-and-simplify-provisioning).
Physical acceptance remains open.

## Delivered behavior

- Removed the permanent device root. Each enrollment has an independently random
  32-byte token; vault binding and journal authentication use separate purpose contexts.
- Allocated the raw status mask to OTP page 3 and tokens to pages 4–60, providing
  228 slots on fresh OTP. Eight three-bit states fit each 24-bit row, using 29 mask
  rows. Tokens use ROM ECC I/O; raw checks include ECC bits.
- Provisioning verifies the token and commits a fresh journal before marking READY.
  Interrupted STARTED slots pass through REVOKING and are fully burned. Previously
  programmed rows are excluded, reducing capacity on used boards.
- Journal version 2 uses a token-derived MAC key. Truly blank journals are EMPTY;
  nonblank journals with no valid record are CORRUPT. Invalid committed records
  deny access even alongside older valid records, preventing attempt-count fallback.
  A READY token with a missing or corrupt journal never resets its attempts.
- Destruction records intent, revokes the token, clears its cached journal key and
  burns all secret rows. After revocation, recovery uses OTP state alone.
- New setup and explicit policy changes offer destruction with 1–100 attempts
  (default ten). Compatible stored lockout policies remain enforced, including
  across credential changes.

Old root-based and experimental journal formats are incompatible and are not
migrated. See [key handling](../docs/KeyHandling.md#token-only-otp-enrollment)
and [setup recovery](../docs/Setup.md#interrupted-setup-and-destruction).

## Validation of the final PR head

| Check | Result |
| --- | --- |
| Standard host suite | 30/30 passed |
| Full host/FIDO suite with AddressSanitizer and UndefinedBehaviorSanitizer | 44/44 passed |
| Full Pico firmware with UI, USB MSC and FIDO | Cross-build passed |
| Physical OTP/ECC and power-interruption acceptance | Pending |

These were local checks, not GitHub CI. The sanitizer build used `-g -O1` with
assertions retained. LeakSanitizer was disabled (`ASAN_OPTIONS=detect_leaks=0`)
because of the sandbox ptrace limitation; no leak-check claim is made.

Coverage includes all 228 enrollments and exhaustion, independent journal-key
derivation checks, slot/device separation, malformed masks, occupied legacy rows,
corrupt/missing/stale journals, provisioning and destruction interruption with
RAM reset, charged attempts across resets and SD rollback, and stored lockout policy.

## Remaining acceptance

Verify actual OTP mask transitions, ECC programming/readback and full burns on the
board, including interrupted provisioning and destruction. Repeat power-cycle,
attempt-limit and journal interruption tests with the merged format. Earlier
September hardware results used the root-based layout and do not qualify this one.

Internal-flash replay within the same enrollment remains outside the agreed
protection scope. Production OTP access, trusted boot/update, debug restrictions
and protected key RAM remain tracked in the
[production checklist](../docs/AI/production-checklist.md). This merge establishes
software completion for the enrollment change, not production readiness.
