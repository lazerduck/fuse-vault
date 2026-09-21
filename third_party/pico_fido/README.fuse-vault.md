# Pico FIDO: V2 engine baseline

Initial source is selectively adapted from the repository's V1 archive:

- Pico FIDO https://github.com/polhenarejos/pico-fido revision
  `09d95a469b3ca1142bb04b505b49ab77eba6e964` (AGPLv3).
- Pico Keys SDK https://github.com/polhenarejos/pico-keys-sdk revision
  `263b2a9839acb3a16936199ca5dbb814e8bf16e1` (AGPLv3).
- Adjacent TinyCBOR encoder/parser from revision
  `c0aad2fb2137a31b9845fbaae3653540c410f215` (MIT); original license retained.

`engine.cmake` is the explicit compiled-source list. Other copied headers and
source files are retained as reference only. Upstream boot, OTP programming,
physical flash, USB drivers, applets, certificates and vendor command dispatch
are not linked. This is an opt-in AGPL-covered library, not a relicensing of
previously unlicensed project code. Distributions must satisfy dependency licenses.

## Inherited adaptations

The archived port supplied injected RNG/time/presence/UV/commit, bounded integer
offsets instead of flash pointers, staged commits at command boundaries, session
cleanup, built-in verification/token handling, local metadata/deletion access,
ES256-only algorithm selection and self-attestation. Full historical adaptation
notes remain in `v1/firmware/third_party/pico_fido/README.fuse-vault.md`.

## V2 changes

- Standalone `fv_fido_engine` target and V2-owned public headers. TinyCBOR encoder
  is linked directly; no legacy probe, runtime, crypto or storage dependency.
- Shared V2 Mbed TLS configuration/implementation, enabled with
  `FV_ENABLE_FIDO_ENGINE`; no second crypto ABI/library.
- Required built-in UV/retry callbacks: missing callbacks cannot select an
  external-PIN device profile. External PIN handlers retained upstream are
  unreachable through the V2 profile.
- Constant-zero signature counters in registration/assertions, with no
  counter-only persistence on login. Matches the accepted storage replay model.
- Reject too-small response buffers and pre-existing cancellation before command
  execution. Public ownership, response sizing and wiping contracts documented.

## Verification and review boundaries

See `src/fido/README.md` and `docs/v2-fido-progress.md` for actual V2 evidence.
V1's encrypted store, USB and hardware claims do not describe this library.
The independent client uses test-only simulated UV/presence and RAM snapshots.
Built-in device-secret verification, encrypted FIDO persistence and browser USB
interoperability remain later stages.

Upstream release notes were inspected on 2026-09-20; they describe newer protocol,
storage, bounds and authorization work. The pinned commit could not be retrieved
through the web reader, so a complete upstream security-diff review remains an
explicit prerequisite before hardware/release readiness, not a completed audit.
Do not label this imported baseline latest, certified, or security-reviewed.

Local UV preference extension: EF_FV_UV_POLICY (0x1123) stores a versioned two-byte
strict/session preference in the encrypted snapshot. The local API requires an
unlocked trusted UI owner, validates both modes, commits before success, and
invalidates host tokens on save. No host CTAP setter is exposed. Missing records
default to strict; invalid records deny engine dispatch. Ordinary FIDO reset
preserves the preference, while local store initialization removes it.
