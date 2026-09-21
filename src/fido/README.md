# Portable V2 FIDO engine

Opt-in development engine and encrypted snapshot-store libraries, with no board
USB/UI attachment yet. Progress
and remaining gates are in [the delivery ledger](../../docs/v2-fido-progress.md).
The imported pico-fido/Pico Keys sources are AGPLv3: see the
[provenance and changes](../../third_party/pico_fido/README.fuse-vault.md).

## Build and test

Use the same Mbed TLS 3.6 checkout as the V2 crypto module. One Mbed TLS
configuration/implementation is shared; FIDO-disabled builds retain their minimal
crypto configuration. FIDO currently enables the broader library primitives used
by the pinned helpers, but the CTAP algorithm profile accepts ES256 only.

```sh
cmake -S . -B build-fido \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fido -j4
ctest --test-dir build-fido --output-on-failure
```

Tests require Python packages `fido2` and `cryptography`; missing packages fail
configuration rather than silently skipping independent signature verification.
For ASan/UBSan configure a separate build with `-DFV_SANITIZERS=ON`. In this
traced environment use `ASAN_OPTIONS=detect_leaks=0`; no leak-check claim is made.

## Boundary

`fido_engine.h` exposes one serialized engine instance with injected entropy,
clock, durable snapshot commit, presence, built-in UV/retries, cancellation, and
optional local-management authorization. It cannot open without built-in UV
callbacks; host PIN setup is rejected. The platform must implement actual unlock,
RP-bound bounded verification reuse, physical prompts, and cancellation polling.
The test executable's RAM commits and simulated input are never firmware sources.

The caller supplies a 64 KiB object image and an independently derived FIDO key,
not the VMK or OTP root. Fresh initialization (all FF) is an explicit caller
choice. Reopening requires a previously authenticated snapshot. The engine alone
does not authenticate external storage or distinguish corruption from a fresh
store; use `fv_fido_store_open` to authenticate and recover external storage. `close` wipes the caller's image and cached
secrets; a persistence/RNG failure faults the session until close/recovery.

Commands are CTAP status + CBOR, with 2048-byte request and 4096-byte response
bounds; this module is not CTAPHID. All callers must serialize access and provide
full-size, nonaliasing response buffers. Durable callback success precedes success
responses. Cancellation already pending at dispatch cannot execute a command;
cancellation during a completed durable commit cannot undo that commit, and must
not be interpreted as proof that registration never persisted.

The profile supports direct built-in UV and protocol 1/2 scoped UV tokens,
ES256 resident/nonresident credentials, credential self-attestation, credential
management, and FIDO reset. Sign counters are always zero; assertions do not
write snapshots merely to increment a counter. UV-token expiry is engine-owned;
device unlock caching/RP binding and per-request physical approval are platform
policy, and remain an integration task. The development AAGUID is not a certified
identity. No HID interface or real-account/browser compatibility is claimed yet.

Reset's ten-second window currently starts at engine open (inherited from V1).
F3 must bind this to USB power/enumeration rather than allow each vault reopen to
restart the host reset window. Reset must use a distinct destructive UI prompt.


## Encrypted snapshot store (F2)

`fv_fido_store` connects the engine's commit callback to the existing V2 vault
pipeline in private sectors 16–2063. It uses separate FIDO keys and two snapshot
banks. See [format, API and recovery policy](../../docs/v2-fido-storage.md).
The encrypted engine test target runs the same independent-client suite against
file-backed encrypted media, including signatures after changing the vault secret.

Store tests cover AES, Camellia and four ordered layers; bank/volume/key binding;
whole-snapshot integrity; read-only open; explicit initialization; no writes to
USB/header regions; stale writers; credential changes; allowed replay; destruction;
and injected partial writes, sync/read failures and lost unsynchronized writes.
Hardware media, firmware scheduling and complete engine teardown on lock remain
F3/F5 acceptance items.

## Firmware integration

F3 opt-in composite firmware and real USB test instructions are in
[`docs/v2-fido-testing.md`](../../docs/v2-fido-testing.md). The persistent
[delivery ledger](../../docs/v2-fido-progress.md) separates implemented software
from pending board acceptance.
