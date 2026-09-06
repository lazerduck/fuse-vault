# Pico FIDO integration

Upstream: https://github.com/polhenarejos/pico-fido
Revision: `09d95a469b3ca1142bb04b505b49ab77eba6e964`.
Original file: `src/fido/cbor_get_info.c` (AGPLv3).

`get_info.c` is the legacy reduced GetInfo adaptation retained as a fallback for
isolated transport tests. The device binds `port.c` and the full engine instead;
its GetInfo describes the implemented credential and built-in verification
features. No upstream entry point, OTP initializer, attestation identity or
hardware flash backend is included. See `LICENSE` and the engine details below.

TinyCBOR's encoder/parser subset is vendored separately from the dependency fetched by
Pico Keys SDK `263b2a9839acb3a16936199ca5dbb814e8bf16e1`: TinyCBOR commit
`c0aad2fb2137a31b9845fbaae3653540c410f215` (v0.6.1), MIT licence. Its files are
unmodified. No configure-time network download or external `/tmp` path is needed.

The optional combined firmware includes AGPL-covered code. Distribution must
comply with that licence and all dependency licences. Adding this component does
not assign a licence to the project's previously unlicensed original files.

## Reusable engine port

`engine/` contains the selected Pico FIDO CTAP2 handlers from the revision above.
`sdk/` contains the selected file/object store and crypto helpers from Pico Keys
SDK `263b2a9839acb3a16936199ca5dbb814e8bf16e1` (AGPLv3). `engine.cmake` lists
exactly which sources are compiled; upstream boot, hardware USB, OTP and flash
implementations are excluded. The SDK's Mbed TLS 3.6 backend is shared, with
FIDO-specific modules enabled conditionally. TinyCBOR parser sources are now
included alongside the encoder, at the same pinned revision.

`port.c` supplies an independently testable synchronous engine with injected RNG,
time, physical-presence and durable-snapshot callbacks. It supports ES256
registration/assertion, resident credentials, ClientPIN protocols 1 and 2,
credential management and FIDO reset. Registration uses credential
self-attestation; enterprise attestation, vendor commands, U2F, unrelated applets
are not exposed by this adapter. Built-in UV is supplied by the device adapter.

The device USB path binds the full engine after successful device unlock.
`fido_store.c` supplies encrypted SD snapshots anchored in the internal journal;
the device approval callback services USB, input, display and connector/media
safety. Built-in verification reuses the bounded, RP-bound device-unlock cache.
The production-facing profile omits external ClientPIN setup, supports direct UV
and permission-scoped UV tokens, and has no separate FIDO PIN. The engine-only
host fixture still exercises the external ClientPIN profile for regression tests.
The complete device fixture exercises real encrypted storage and runtime unlock.

All host fixtures use simulated presence; their approval callbacks are never
linked into the device. Hardware interoperability, latency, power-cut validation,
security review and certification remain release gates.

Local modifications to upstream sources:

- Replace platform selection, constructors, board time and hardware queues with
  the Fuse Vault callback boundary; suppress engine diagnostics.
- Retain COSE helpers while replacing the upstream dispatcher; remove APDU/U2F
  applet registration and certificate generation. The adapter owns GetInfo.
- Restrict MakeCredential algorithm selection to ES256.
- Use 32-bit serialized file offsets on both 64-bit hosts and RP2354; add bounded
  record scanning. This is an adapter format, not an upstream firmware backup.
- Propagate file/crypto initialization errors, clear cached device keys at init,
  add ClientPIN session cleanup, and invalidate authorization on close.
- Stage upstream flash commits and invoke the durable callback once at the end
  of initialization or a command. Failed commits fault the session before any
  success response; the caller must reopen from its last durable snapshot.

This is an integration port, not a security audit or FIDO certification.

Additional device-integration changes add built-in UV/token dispatch and shared
retry reporting, channel-aware engine requests, cancellation cleanup, bounded
credential-enumeration state, reset-window timing relative to FIDO enumeration,
and complete authorization invalidation on reset. No upstream hardware driver
or OTP initializer is linked. The fixed offset store is encrypted by Fuse Vault
before SD writes; Pico FIDO does not receive the vault's plaintext block device.

Local management adaptation: `port.c` exposes a device-session-authorized
resident credential metadata/deletion API. It serializes against host commands,
uses stable resident IDs and stages deletion until the existing durable commit.
It never supplies private keys or creates CTAP authorization tokens for the UI.
