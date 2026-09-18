# V2 storage and device-state boundary

Agreed scope: controlling, cloning, replacing or restoring SD contents must not
reveal secrets, reset authentication controls, buy extra attempts or undo
cryptographic destruction. Destruction/corruption and
restoration of valid historical user data are acceptable. FIDO is deferred.

## State placement

- **SD:** encrypted payloads, packed sector tags/unset markers, and eventually
  authenticated volume configuration, wrapped VMK and encrypted private objects.
- **Device persistent state:** authoritative attempt accounting and revocation;
  credential_generation, exact committed-header hash and authenticated policy
  to prevent SD rollback from undoing authentication changes. Default policy is
  ten attempts with cryptographic destruction; accounting and limit action are
  separate mechanisms. These must not be reset by presenting a different SD.
- **Device root:** protected device-local secret. Exact OTP/flash allocation,
  access restrictions and provisioning remain to be implemented and validated.
  Internal placement alone is not a claim of physical tamper resistance; firmware
  and debug/access policy are part of the protection.
- **RAM session:** recovered VMK/derived keys, pipeline and metadata cache; cleared
  on lock/session teardown by their owners.

Critical state can use small persistent records to survive interruption. This
does not authorize per-sector anti-rollback counters or duplicate bulk data.
A copied SD header containing an old password's VMK wrapping is a credential
rollback concern separate from ordinary data replay. credential_generation changes
only for authentication-envelope/policy updates, never data or private-object writes.

## Implementation status

Implemented and hardware-benchmarked previously: AES/Camellia XTS pipelines,
four-bit SD and laptop-controlled USB transfers using public test keys.

Implemented: HMAC-SHA-256, packed sector metadata, explicit unset handling, bounded clean
metadata caching, and protocol-4 benchmark instrumentation. Desktop corruption
and interrupted-write tests pass. The authenticated hardware smoke test and
[paired 20-configuration benchmark](../results/v4-authenticated-baseline.md)
passed on 2026-09-16; hardware fault injection remains untested.

The portable volume/envelope/session implementation is described below. The
benchmark firmware does not yet use it and still has public test keys. It does not
write RP2354 security flash or OTP. Production entropy, protected device authority
and authentication UI remain required before real vault use.


The protocol-5 benchmark adds a Pico SHA-256 backend with a mandatory on-board
HMAC self-check and backend identification in results. Desktop adapter, storage
and integration tests pass. The board self-check and all ten accelerated
authenticated configurations also passed on 2026-09-16; see the
[hardware comparison](../results/v5-sha-comparison.md). HMAC time fell about 84%,
although slower AES encryption in this build limits write gains. This changes
implementation/performance, not tag format or policy.


Both cipher hot paths and Camellia lookup tables are now configured for SRAM
in the board build (`FV_CIPHERS_RAM=ON`), adding 3,760 initialized RAM bytes.
The combined UF2 passes desktop tests and all 30 cases in the
[three-pass board benchmark](../results/v5-ciphers-ram-benchmark.md), with stable
cipher timings. Hardware fault injection remains untested.
This completes the assembled benchmark data path (USB, cipher pipeline,
HMAC/metadata, SD), not the production volume/key/unlock lifecycle listed above.

## Portable vault lifecycle (2026-09-17)

The [volume design](v2-volume-and-key-design.md) and
[VMK envelope](v2-vmk-envelope-proposal.md) now have a portable implementation:
one to four fixed slots, XOR shares, AES/Camellia KW, outer HMAC, binding/KDFs,
create/unlock/read/write/lock, credential change, header repair and charged attempts.
The [security module README](../src/security/README.md) documents ownership and APIs.

Fourteen release and fourteen ASan/UBSan tests pass, including independent fixed
vectors and file-backed lifecycle/failure injection; ARM library compilation passes.
Device authority is a callback contract with mock test persistence, not a production
flash journal. Physical destruction, OTP provisioning, production RNG and KDF
calibration remain pending. The persistent format is unfrozen and independent
security review is still needed. No device was flashed or provisioned by this work.

Device loss may destroy the vault; no root/VMK export or recovery mechanism is
introduced. The 1 MiB private-object reservation remains. Future FIDO keys are
non-exportable and use the vault hierarchy; two devices register independently.
Historical encrypted object replay remains accepted unless it bypasses device
authentication. No whole-object-store freshness mechanism is added.

## First Pico integration image

[Security bring-up](../firmware/security/README.md) links the complete portable
lifecycle to SD/SHA/cipher backends and a candidate TRNG/CTR-DRBG adapter. It adds
compile-time removable read-only OTP inspection and a laptop snapshot/diff tool.
Its test enrollment is deliberately volatile: no permanent OTP slots are assumed,
no OTP/security-flash writes occur, and no power-cycle durability is claimed.
Capture the physical inventory and measure entropy/KDF operation before committing
the permanent enrollment layout. Seventeen host/sanitizer tests and both ARM build
variants pass. The [first board run](../results/security-bringup-20260917.md) now
passes RNG, 60,000-iteration KDF timing and three complete SD lifecycle tests.
Median envelope unlock is 1.40151 seconds. OTP baseline/after snapshots match in
all reported fields; no OTP writes were performed. Persistent authority and
irreversible enrollment tests remain next-stage work.

## Persistent integration implementation (2026-09-17)

The sections above record earlier milestones. The current development image adds
[OTP root/token enrollment and authenticated flash authority](v2-persistent-authority.md),
with explicit provisioning, revoked-first destruction, token rotation and recovery.
Enrollment uses exactly 60,000 PBKDF2 iterations. Nineteen host tests pass, including
byte-cut flash failures and a full vault lifecycle using the persistent adapter
model; both ARM debug-option variants build. Physical provisioning and power-cycle
tests have not yet run. The [production checklist](production-checklist.md) tracks
hardware validation and access protections separately from implemented code.
