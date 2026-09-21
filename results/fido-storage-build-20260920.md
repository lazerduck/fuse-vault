# V2 encrypted FIDO store validation — 2026-09-20

F2 portable store implementation and test integration. No board was flashed and
no physical SD or OTP was changed. Format/API documentation is in
[the storage design](../docs/v2-fido-storage.md); staged progress is in
[the delivery ledger](../docs/v2-fido-progress.md).

## Changes

- `fv_fido_store` uses private sectors 16–2063, two independently keyed banks,
  the vault's selected XTS cipher layers, and existing V2 ciphertext sector HMAC.
- Domain-separated engine/bank/integrity/manifest keys derived from unlocked VMK
  and authenticated volume descriptor. No keys retained in the store handle.
- Explicit destructive initialization; opening is read-only and never implicitly
  formats. Authenticate manifests, every sector and the complete ciphertext image.
- Write/sync/readback before publishing a new manifest; stale writers and ambiguous
  failures fault the session. Internal ACTIVE authority is checked before access.
- No internal anti-rollback anchor or snapshot flash commits. Valid SD replay is
  deliberately allowed; destruction/attempt state remains internal authority.
- Actual pico-fido engine connected through its durable callback to file-backed
  encrypted media in a test target. Normal firmware attachment remains F3.

## Host validation

```sh
cmake --build build-fido -j4
ctest --test-dir build-fido --output-on-failure
cmake --build build-fido-sanitize -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-fido-sanitize --output-on-failure
```

The suite now has 30 CTest cases. The final full normal/sanitizer runs passed the
29 other cases, including both encrypted engine tests. The store fixture was then
corrected to distinguish rejection by internal authority from wrong-volume
cryptographic rejection after the new authority guard, and rerun separately:

```sh
ctest --test-dir build-fido -R '^fido_store$' --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-fido-sanitize -R '^fido_store$' --output-on-failure
```

Both focused store reruns passed. All 30 cases are validated in normal and
ASan/UBSan builds across the full suite and focused rerun.
LeakSanitizer is disabled in this traced environment; no leak-check claim.

Coverage includes:

- AES, Camellia and four ordered layers; USB/header region preservation; USB file
  round-trip; key-domain separation; FIDO image/key preservation across credential
  rewrap; locked/pending-destruction authority denial; full destruction then SD replay.
- 370 commit interruption cases: all 36 write calls (zero/17/512/4095-byte partial
  writes), four sync calls and 37 read calls, each with unsynchronized writes either
  retained or discarded on simulated power loss. Recovery accepts only complete
  old/new images. Includes bank reuse with previously valid inactive contents.
- Initialization interruptions, every open-read failure/output wiping, silent
  payload/tag/manifest write corruption, wrong VMK/volume/bank, stale writers,
  latest committed payload corruption, valid whole-image rollback, and replayed
  authentic sectors/tags beneath a different valid manifest.
- Independent python-fido2 client verifies real ES256 signatures after encrypted
  reopen and vault credential change, both UV-token protocols, resident/nonresident
  credentials, denial, permissions, token expiry, deletion/reset, full store,
  zero-counter/no-write assertions, real SD-write failure and malformed requests.

The test media models a synchronized file checkpoint and loss/retention of pending
writes. Authority/UI/entropy fixtures are test-only; this is not physical flash,
SD controller power-loss, trusted UI or USB compatibility validation.

## ARM compilation and linking

The existing Cortex-M33 build configuration compiles `fv_fido_store` and its V2
security/storage dependencies. A temporary standalone main calling both store
open and engine command linked with the engine/store/security/storage/crypto
archives, `--specs=nosys.specs -Wl,--gc-sections -lm`.

Standalone smoke ELF: text 275,912 bytes; data 2,228; BSS 123,108. Includes a 64 KiB
image, a vault session and response buffer, but not the real board USB/UI/worker
allocation. Newlib emitted its expected standalone syscall-stub warnings. It is
not a hardware firmware image or a measured runtime memory budget.

Compiler-reported individual store frames: write snapshot 12,816 bytes, open
11,704 bytes. These stack allocations are wiped on return. Combined engine call
chains, worker scheduling, heap demand and stack watermarks require F3/F5 checks.

## Next

F3: integrate standard HID alongside MSC, use the store from the single worker,
attach real unlock/reverification/approval and close both engine/store on shared
session invalidation. No store initialization may be triggered by a host command
or by recovery failure. Add bounded scheduling around storage batches and UI waits.
