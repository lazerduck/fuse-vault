# FIDO2 integration: implementation and remaining validation

Updated 2026-09-06. Pico FIDO is connected to the RP2354 application, USB owner,
unlock lifecycle and encrypted SD storage. It remains opt-in and release-gated
until physical-device validation. No board has been flashed or tested.

## User flow and authorization

With `FUSE_VAULT_ENABLE_FIDO2=ON`, the provisioned device asks for its existing
unlock secret before offering the storage/FIDO mode selector. Successful unlock
must be durably recorded before either interface attaches. Selecting FIDO exposes
HID only; selecting storage exposes MSC only. Leaving either mode locks the
device. A host command cannot switch modes or obtain the vault block interface.

The existing unlock is the built-in FIDO verification method. There is no second
FIDO PIN to enroll or enter, and the device profile does not offer external
ClientPIN setup. A fresh, debounced Select press approves registration/signing;
Back cancels. Held mode-selection buttons cannot approve a request. Reset has an
explicit erase-credentials prompt and requires approval within ten seconds of
entering/enumerating FIDO mode. Reset leaves the vault intact.

Verification reuse follows the base cache bounds in [FIDO security requirement
3.4](https://fidoalliance.org/specs/fido-security-requirements/fido-authenticator-security-requirements-v1.6-fd-20250312.html):
first use within 30 seconds of unlock, a maximum completion age of ten minutes,
and binding to the first relying party. Expiry or a different RP requires the
same device unlock again. Fresh presence is collected separately for credential
operations. The runtime clears verification, keys and USB state on lock, host
unmount/suspend, media failure or connector fault. This is an implemented policy,
not a certification claim.

The destructive attempt policy is shared because the credential is shared.
Failed device unlocks use the existing durable attempt reservation and revocation
path. FIDO commands cannot set or change that secret, replenish its attempts or
unlock storage. Root revocation makes both vault and FIDO data inaccessible.

## Storage and transaction boundary

`fido_store.c` opens the authenticated layout's private FIDO reservation. It derives
separate, versioned engine and storage keys from the unlocked VMK and vault ID.
It uses the **same encrypted-block implementation and selected encryption stack**
as the vault. Neither the original VMK nor raw OTP roots are passed to Pico FIDO.
The reservation is not present in the MSC geometry.

The 1 MiB physical reservation provides 256 KiB through the existing 4:1 encrypted
block format. Two 64 KiB snapshot banks hold Pico FIDO's adapted file/object image.
A commit writes and verifies the inactive bank, synchronizes SD, then appends its
KMAC digest to the dual-authenticated internal security journal. Journal recovery
must confirm the new digest before the operation returns success. The engine
stages its file updates until the end of a command. On restart, only the bank
matching the internal anchor is accepted. Restoring old SD media cannot undo a
committed deletion/reset or substitute credentials. Missing/corrupt anchored data
fails closed; it is never silently reformatted.

Journal v1 records remain readable. The first FIDO commit writes v2 using the
previously reserved authenticated bytes (initialized flag at byte 50 and digest
at bytes 51–82). Later state updates preserve the anchor; journal append rejects
clearing it. Unsupported older firmware must not be installed after migration:
firmware rollback prevention remains part of the existing secure-boot/update
release policy. This protects against SD replay, not arbitrary rollback of both
internal flash and firmware.

Each changed snapshot currently writes 128 encrypted logical sectors and consumes
one internal journal record. This prioritizes correctness and is a performance
and flash-endurance measurement item. The implementation does not claim measured
latency, endurance or a guaranteed resident-credential count.

## Engine, transport and build

Pinned sources and local changes are in
[`README.fuse-vault.md`](../firmware/third_party/pico_fido/README.fuse-vault.md).
The selected Pico FIDO handlers implement ES256 MakeCredential/GetAssertion,
resident and non-resident credentials, credential management, reset, direct
built-in UV and permission-scoped UV tokens for protocols 1 and 2. Credential
self-attestation is used. Vendor commands, U2F and enterprise attestation are not
exposed. The separate engine-only host fixture retains external ClientPIN tests;
its auto-approval and RAM snapshot callbacks are never installed on the device.

The existing TinyUSB owner dispatches CBOR to the engine. Its historical
`fido_probe` name now also covers the bound CTAPHID transport. Requests are limited
to 2 KiB by the engine; transport responses can span 4 KiB. Processing emits
keepalive reports; CANCEL and same-channel INIT abort/resynchronize the operation;
other channels receive busy errors. Presence waits and snapshot writes poll USB,
media, input and connector safety. Crypto calls are synchronous; their maximum
service latency still needs measurement on the board.

The FIDO build uses the existing Mbed TLS backend with the needed modules enabled.
It reserves a guarded 64 KiB main-SRAM stack using Pico SDK 2.3 linker overrides.
Compiler reports found a roughly 26 KiB display frame nested below a roughly
6.7 KiB assertion frame, so the SDK's scratch stack was insufficient. Link-time
assertions separate heap and stack. Hardware stack watermarks remain required.

## Validation performed without hardware

- Application/runtime tests require unlock before FIDO attachment and clear the
  session on exit. Existing storage regression tests remain enabled.
- An independent `python-fido2` client traverses runtime provisioning/unlock,
  CTAPHID fragmentation, the Pico FIDO handlers, encrypted file-backed SD and the
  internal journal fixture. It verifies registration/assertion signatures,
  credential persistence after lock/reopen, RP binding, expiry, deletion and
  reset for both PIN/UV protocol versions, without a separate FIDO PIN.
- Store tests reject wrong keys and old authentic SD snapshots. Interrupted
  snapshots at several sector boundaries and a failed journal commit recover the
  previously anchored image. Existing encrypted-sector/journal fault tests cover
  their lower-level write boundaries.
- Transport tests cover malformed framing, sequence/time limits, busy channels,
  cancellation, resynchronization, backpressure and exclusive MSC/HID ownership.
- FIDO engine, complete simulated device, transport and runtime tests pass under
  AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer is disabled in
  this traced environment; no leak-check result is claimed.
- RP2354 builds with FIDO enabled and disabled link successfully. The optional
  combined release configuration remains rejected.

## Remaining hardware/release acceptance

1. Validate the existing SD-detect, connector-mux and USB-presence electrical
   gates, then enumerate HID and MSC exclusively on each supported host OS.
2. Perform browser WebAuthn registration/login and credential-management trials;
   verify physical approval, held buttons, cancellation, reset and reconnect.
3. Measure crypto/USB service latency, full-snapshot write time, heap/stack peaks,
   flash endurance and UI responsiveness on the actual board.
4. Cut power during SD and internal-journal writes, remove cards, suspend hosts
   and exercise connector conflicts. Verify recovery against the committed
   internal anchor without releasing stale credentials.
5. Complete protocol/conformance and security review, firmware anti-rollback,
   release identities and dependency/licence distribution requirements before
   removing the release gate. Development USB IDs and AAGUID are not certified
   or production-assigned identities.
