# V2 volume format and key lifecycle — review draft

**Status: four-slot share envelope and portable vault lifecycle implemented and tested.
TRNG/DRBG, OTP enrollment/destruction and flash authority are implemented for
hardware testing; production access protections remain pending.
The persistent format is NOT frozen.**

This is the next step after the working, benchmarked USB → cipher stack →
HMAC/metadata → SD path. It replaces public benchmark keys with a persistent,
random vault master key (VMK), a protected header and a real session lifecycle.
It does not add FIDO, a filesystem, per-sector rollback protection or duplicated
payloads. The existing benchmark firmware remains unchanged.

## 1. What this gives us

Create a volume once, write data, restart the device, unlock it, and reconstruct
exactly the same pipeline and keys. A password change rewraps the VMK; it does
not re-encrypt the data. Locking removes the working keys from RAM.

There are three distinct types of key:

| Key | Purpose | Lifetime/location |
|---|---|---|
| Device root, 32 random bytes | Binds unlock to this device | Protected device state; never SD |
| VMK, 32 random bytes | Root of this volume's working keys | Wrapped on SD; temporarily in RAM |
| KEK, 32 derived bytes | Encrypts/authenticates the stored VMK | Temporary during create/unlock/change |

Working keys derived from the VMK serve each XTS layer and sector HMAC separately.
The credential is not a data-encryption key. Slow password derivation runs only
at authentication, never during reads or writes.

The security boundary stays as agreed in [device-state placement](v2-security-state.md):
SD possession/manipulation must not reveal secrets or reset authentication.
Deletion, corruption and replay of valid historical data are accepted. Device-root
protection, trusted firmware and debug policy are necessary assumptions; simply
putting a secret in flash does not establish those protections.

## 2. Decisions and remaining gates

Accepted: random device root and VMK; device-bound credential derivation; separate
working keys; no separate password verifier; credential changes preserve VMK;
explicit session erasure; device-local charged attempts and header authority.

- PBKDF2-HMAC-SHA-256 is the first KDF to implement/benchmark. Production iteration
  counts remain unset until actual RP2354 calibration targets 1–2 seconds, about
  3 seconds maximum. KDF ID/version remain explicit.
- AES-GCM is **not** the sole/default VMK wrapper. The
  [stack-aware envelope candidate](v2-vmk-envelope-proposal.md) specifies a
  shares + standardized key-wrap + outer-HMAC construction. The user accepted
  shares and four slots; portable implementation and independent reference vectors
  are present. Independent security review remains outstanding.
- Authentication policy is configurable and anchored. Default: ten attempts,
  **cryptographic destruction**. Persistent lockout has a separate action ID;
  future delay actions are reserved, rejected until implemented. No unlimited mode.
- Device failure/root loss may destroy access. No recovery keys, VMK export,
  manufacturer/cloud recovery or cross-device root cloning are introduced.
- One active vault per device initially. Keep the 1 MiB future-object reservation.
- The [device-state/destruction proposal](v2-device-state-proposal.md) defines the
  journal, policy codec, final-attempt semantics and irreversible token invalidation
  candidate. Physical OTP access/lock sequence and record encoding require review.

`credential_generation` means **which authentication envelope is authoritative**.
Advance it on credential/profile/KDF changes, rewrap/envelope replacement and
relevant authentication-policy changes. Never advance it for file writes/deletes,
sector tags, FIDO/SSH object creation/deletion or ordinary private-object activity.
It is not a whole-volume generation. It must not wrap around.

The boundary also explicitly prohibits extra attempts or revival after destruction
through SD rollback. Historical encrypted data/private objects may be replayed
where they do not bypass device authentication. No whole-volume/object hashes,
Merkle trees or per-write monotonic counters are added.

Standard primitive references: [PBKDF2, RFC 8018](https://www.rfc-editor.org/rfc/rfc8018.html)
and [HKDF, RFC 5869](https://www.rfc-editor.org/rfc/rfc5869.html). The application
protocol is our review-stage design, not a standards-certified composition.

## 3. Encoding rules

All offsets below are byte offsets. Integers are unsigned little-endian.
Encode/decode explicitly; never persist a C struct, its padding or pointers.
Ranges written as `a:b` include a and exclude b.

`||` means concatenate. `LE16`, `LE32`, `LE64` encode exactly that many bits.
`L(s)` means the exact ASCII bytes of the quoted label followed by one zero byte.
Labels are case-sensitive. Hashes are SHA-256; HMAC tags are full 32 bytes.

The storage library takes a byte string plus explicit length for `user_secret`:
no implicit terminator, trimming or Unicode conversion. Propose nonempty, at most
256 bytes. Text input profile 1 is UTF-8 without normalization. Navigation input
profile 2 is a sequence of bytes: up=1, right=2, down=3, left=4, select=5; back
edits input and submit is out-of-band. The UI must use the selected profile
consistently. Secret input comes from the on-device UI in production; desktop
secret input is only a test adapter. Neither logs credentials nor derived keys.

## 4. SD layout

Physical and logical sectors remain 512 bytes. This profile uses the whole SD as
a private vault backing store; a future USB mass-storage host sees only decrypted
logical payload sectors, never these internal headers or metadata.

| Physical sectors | Use |
|---|---|
| 0 | Header slot A, one 512-byte record |
| 1–7 | Reserved, not exposed |
| 8 | Header slot B, same format |
| 9–15 | Reserved, not exposed |
| 16–2063 | 1 MiB reserved for future private objects |
| 2064 through 2064+M−1 | Existing packed sector metadata |
| 2064+M through 2064+M+N−1 | Encrypted host payload |

`N` is logical payload sector count; `M = ceil(N/15)`. Choose the largest N that
fits `2064 + M + N <= physical_capacity`, or an explicitly smaller capacity.
Check every arithmetic operation for overflow. The header records N, M and the
bases; the decoder checks them against these formulas and actual card capacity.
Larger cards may hold a restored volume without changing N; smaller cards must
still fit the recorded layout. Physical SD serial numbers are not key inputs.

There is no automatic format on open failure. Creation initializes metadata to
unset and does not erase or encrypt the whole payload. The private reservation
is inaccessible until a future authenticated object format is defined.

Two tiny header copies support controlled updates, not old/new copies of user
data. Their separation is not a promise about SD physical erase boundaries or
power-loss isolation.

## 5. Immutable descriptor: first 128 bytes of a proposed 512-byte header

The first 128 bytes are an **immutable volume descriptor**. They identify how
existing payload is encrypted. Credential changes must leave these bytes intact.

| Offset | Bytes | Field / required value |
|---:|---:|---|
| 0 | 8 | Magic: ASCII `FV2VOL01` |
| 8 | 2 | Format version = 1 (V2 project's first persistent format) |
| 10 | 2 | Header bytes = 512 |
| 12 | 4 | Flags = 0 |
| 16 | 16 | Random volume ID |
| 32 | 4 | Sector bytes = 512 |
| 36 | 2 | Metadata layout ID = 1 (15 slots, full tags) |
| 38 | 2 | Working-key derivation ID = 1 (section 6) |
| 40 | 8 | Logical sector count N, nonzero |
| 48 | 8 | Metadata base = 2064 |
| 56 | 8 | Metadata sector count M |
| 64 | 8 | Payload base = 2064+M |
| 72 | 8 | Private reservation base = 16 |
| 80 | 8 | Private reservation count = 2048 |
| 88 | 1 | Cipher layer count, 1–4 |
| 89 | 1 | Integrity ID = 1 (HMAC-SHA-256) |
| 90 | 2 | Reserved = 0 |
| 92 | 8 | Four LE16 cipher IDs in encryption order; unused entries zero |
| 100 | 28 | Reserved = 0 |

The mutable authentication/envelope area is specified separately in the
[wrapper proposal](v2-vmk-envelope-proposal.md). It replaces the former fixed
AES-GCM ciphertext/tag/nonce layout. Only the 128-byte descriptor codec is
implemented; decoding it is not authentication and must not open a store/session.
Descriptor magic/version/size values remain provisional until the complete format
is reviewed, despite having executable tests.

Cipher IDs match the current library: 1 AES-256-XTS, 2 Camellia-256-XTS.
Repeated IDs are permitted within four layers; layer index separates keys.
Raw/no-cipher remains a benchmark mode, never a provisioned-vault choice.
Unsupported IDs, flags or nonzero reserved bytes are errors, not requests to
silently choose defaults. Cipher order, N and layout are immutable for this
format: changing them requires an explicit migration/recreation, not a header edit.

The header is readable, not secret. It exposes configuration, size, public IDs,
salt and wrapper metadata. This proposal does not hide those properties.

## 6. Exact proposed derivations

Use tested library primitives; do not implement a new cipher or KDF construction.
`H` below is the serialized header, with public fields already populated.

### 6.1 Device binding and password hardening

The standalone PBKDF2-HMAC-SHA-256 primitive is implemented with a fixed 32-byte
output. Its caller must enforce trusted KDF cost bounds before calling it. No
production iteration count or disk-controlled unrestricted loop is authorized.

The complete bound-input derivation is in the
[envelope candidate](v2-vmk-envelope-proposal.md): an enrollment token plus root
produce a device-local binding key, then a domain-separated HMAC binds the secret,
generation and authentication configuration before PBKDF2. The token is required
for the proposed destruction guarantee. These derivations are implemented and
covered by independent fixed vectors; the hardware token adapter remains pending.

### 6.2 VMK envelope

Use the explicit versioned construction ID, independent wrapper keys, independent
authentication key and full header/envelope coverage specified in the candidate.
No XTS wrapping, AES-GCM-only default or unauthenticated candidate VMK release.
The implemented wrapper and independent reference vectors use four fixed slots.
The platform CSPRNG must provide VMK, salts and any shares;
RNG failure aborts, with no clock/serial/test-seed substitution.

### 6.3 Working keys after successful unwrap

```
D = SHA256(H[0:128])
volume_prk = HKDF-Extract(salt=volume_id, IKM=VMK)

layer_key[i] = HKDF-Expand(volume_prk,
    L("FV2/xts-layer/v1") || D || LE32(i) || LE16(cipher_id[i]), 64)

sector_hmac_key = HKDF-Expand(volume_prk,
    L("FV2/sector-hmac/v1") || D, 32)
```

`i` starts at zero, in encryption order. Each 64-byte XTS key uses bytes 0–31
as the data key and 32–63 as the tweak key, matching the existing module. Retain
its equal-half/duplicate-key rejection. Decryption reverses the layer order.

Neither the credential generation, password salt nor mutable envelope enters these
working-key labels. Rewrapping the same VMK therefore preserves all sector keys.
The immutable descriptor binds keys to the selected configuration. Do not derive
future FIDO/signing private keys from these labels; private objects are deferred.

XTS tweak remains `LE64(logical_LBA) || eight zero bytes`. Sector HMAC and metadata
remain exactly as [implemented](../src/storage/README.md): domain bytes
`46 56 2d 53 45 43 54 4f 52 2d 4d 41 43 00 00 01`, volume ID, LE64 logical LBA,
then the final 512-byte ciphertext. Authenticate written data before decryption;
unset sectors return zeros without payload reads. No per-sector KDF or RNG.

## 7. Device persistent state and header selection

**Proposed logical state**, separate from the SD header:

- Root secret and public device ID (provisioned independently of SD).
- Active volume ID and credential generation.
- SHA-256 of the exact committed 512-byte header.
- Consecutive charged-attempt count, pending marker and authenticated policy.
- Destruction/revocation lifecycle state and enrollment-token slot.
- Monotonic record sequence for local journal recovery; no wraparound.

The [persistent-authority implementation](v2-persistent-authority.md) specifies
flash encoding, allocation, OTP lifecycle and conservative failure recovery.
Physical tests and production access policy remain pending. The portable API requires recoverable, durable state updates:
a restart sees the previous or new valid snapshot, never a fabricated reset to
zero attempts. Corrupt/missing state on an enrolled device fails closed. State
must not be automatically recreated from SD. MAC/CRC checks alone cannot prevent
replay of physically captured internal flash; firmware/access controls are part
of the assumed device boundary.

At open, read only the two fixed header slots. Validate bounded syntax, public
IDs and local generation, then require `SHA256(header)` to equal the committed device
hash. Accept either slot if it exactly matches. Identical duplicates are fine;
there is only one authoritative header identity. Do not select the highest
SD-supplied generation or try an older header after failure.

The trusted hash pins configuration before a password attempt. The proposed
outer envelope HMAC independently authenticates the credential-derived unwrap
and associated header. Device hash comparison is not user authentication.

This is **credential-state rollback protection**, not data freshness tracking.
It changes only during provisioning/credential updates, never per data write.

## 8. Create, unlock, lock and credential changes

### Create (explicitly destructive)

1. Require the device's authorized provisioning state; do not let SD replacement
   authorize reenrollment or clear a lockout. An existing vault needs explicit
   authenticated replacement policy.
2. Select the immutable configuration, validate capacity and generate VMK, volume
   ID, salt and wrapper-required randomness. Allocate the device enrollment token
   under the reviewed provisioning policy. Construct header and working keys.
3. Initialize metadata to unset. Wait for backend completion. Never publish a
   usable volume before initialization completes.
4. Write both matching headers, sync, read them back and verify their exact bytes
   and authenticated unwrap. This small, infrequent check is deliberate; it does
   not add readback to ordinary sector writes.
5. Durably commit the device anchor/state, then report creation complete. On error
   expose no session; incomplete provisioning needs explicit retry/reformat.

### Unlock

1. Read/validate/select the anchored header; check local revocation and lockout.
2. Atomically increment and durably commit the charged-attempt count **before**
   credential evaluation. If persistence fails, do not evaluate credentials.
3. Run device binding, password KDF and authenticated unwrap. The final permitted charged
   attempt may still succeed. On failure/interruption of that final attempt, execute the anchored
   limit action (default: destruction); do not permit another evaluation. Rebooting
   mid-attempt consumes that attempt. Resume pending destruction before any unlock,
   even without an SD. See the device-state proposal for the commit state machine.
4. On failure clear secrets and keep the charge. Wrong credential/unwrap failure
   has one user-facing authentication error. Malformed/unanchored headers fail
   earlier as media/configuration errors without running the KDF or resetting counts.
5. On success durably reset the count, derive working keys and open the existing
   pipeline/store. Publish the session only when all steps succeed. A transport
   or setup error clears it; no partially initialized session escapes.

### Session and lock

One owner manages the storage session; a second core is not a separate trust
boundary. Gate new requests when locking, finish or fail the in-flight operation,
then clear VMK, working contexts, HMAC pads, intermediate derivation values,
plaintext buffers and caches with non-optimizable zeroization. Drop KEK/password
intermediates immediately after use. Retain VMK only while unlocked if needed for
credential changes; never expose it through USB or a general application getter.
Media removal invalidates the session. Reopen/revalidate after replacement.

### Change credential (same VMK and payload)

1. Require current authenticated authorization; serialize against I/O and lock.
2. Build a new header with generation+1, new salt, wrapper-required randomness
   and new credential KEK, but identical descriptor and VMK. The derivation policy may be upgraded here.
3. Write one header slot while preserving the old committed slot. Sync and
   read back/verify the new record.
4. Atomically commit new generation/header hash to device state. **This is the switch
   to the new credential.** No success acknowledgement before durable commit.
5. Mirror the new header into the second slot. If this fails, report that the
   credential changed but header redundancy needs repair; do not revert the anchor.

Power loss before step 4 leaves the old credential selected. After step 4 only
the new credential is selected, even if the other slot contains an old header.
If the card loses the committed header despite reported completion, fail closed;
we do not promise SD durability or silently accept the old password. Small record
journalling is appropriate here, without introducing payload duplication.

## 9. Backups and revocation: consequences to review

Restoring historical data/tag sectors under the same volume identity remains
allowed. After a password change, restoring the old **whole card**, including its
old header, fails the new device anchor. Retain the current header and use it
with historical payload/metadata to restore that data under the new credential.
Provide an explicit restore workflow later; do not require users to hand-edit cards.

Initial revocation blocks the enrolled volume locally and persists across SD
swaps. Reset/reprovision must not become an authentication bypass. The default
limit action irreversibly invalidates the enrolled vault
after the final failed/interrupted attempt; it is not implemented by deleting its
SD header. Persistent lockout is a distinct optional policy with no initial recovery
bypass. Device loss is accepted; no cross-device recovery/export is introduced.
Desktop mock state proves logic, not physical protection.

Policy changes use the same credential-generation/header transaction and require
authentication; weakening requires explicit fresh authentication/physical confirmation.
Restoring an old header cannot restore a weaker policy.

Future FIDO uses the same vault/root architecture, on-device generated non-exportable
keys, composite MSC + FIDO HID and physical approval as appropriate. Its record
format is deferred. Valid historical object replay is accepted; object changes do
not advance credential_generation. Two devices back up ordinary files independently;
for FIDO they register independent credentials with each relying party, not copies
of an exported private key.

## 10. Module/API boundary and acceptance tests

Proposed portable modules: `volume_header`, `key_derivation`, `vmk_wrap`,
`vault_session`; injected interfaces for RNG, device-root operations, durable
security state and the existing block device. Hardware protection stays in the
Pico adapter. No production root value is returned to the USB/application layer.

Conceptual API (names/signatures to refine at implementation):

```c
vault_create(device, secret_bytes, secret_length, config);
vault_unlock(device, secret_bytes, secret_length, &session);
vault_read(&session, lba, count, buffer);
vault_write(&session, lba, count, buffer);
vault_change_credential(&session, new_secret, new_length);
vault_lock(&session);
```

No changes to the existing benchmark protocol are required to review this design.
Desktop tests can use a file-backed card plus separately persisted mock device
state; deterministic RNG is available only in tests. The same session code then
runs on the Pico once entropy and durable device-state adapters are implemented.

Acceptance before real use:

- Standard KDF/key-wrap known-answer vectors and fixed project vectors for each label,
  encoding and full header; independent reference implementation comparisons.
- Create/write/close/restart/unlock/read; wrong credential, wrong device/root,
  header tampering, unknown IDs, overflows and malicious iteration counts rejected.
- Credential change preserves payload and derived keys; old header/password cannot
  undo the change. Current header plus historical valid data remains readable.
- Every attempt debit survives restart; interrupted attempts, tenth-attempt success,
  failed state writes, corrupt journal and SD swaps cannot buy extra guesses.
- Inject interruption at each header/state update boundary with the desktop backend;
  verify the selection rules above. SD fault injection remains a later hardware test.
- Lock/media removal clears secrets and blocks new requests; failed reads expose
  no unauthenticated plaintext. Existing corruption/metadata-boundary tests still pass.
- Benchmark actual create/unlock/rewrap latency and RAM use, then confirm steady-state
  storage throughput remains comparable to the established RAM-cipher baseline.

**Next integration order:** review the implemented portable protocol and platform
contracts; validate entropy/DRBG and calibrate KDF on the board; implement protected
device state and destruction; integrate the on-device credential UI. Budget RAM and
stack before enabling the lifecycle in firmware. No production security claim until
these platform dependencies are implemented and tested.

## 11. Implementation delivered with this revision

- `src/storage/volume_format.c`: strict little-endian descriptor codec with
  fixed/reserved-field, cipher, overflow and capacity checks.
- `src/security/policy.c` and `key_derivation.c`: policy codec, HKDF/PBKDF2 and
  independent working keys, over the existing software/Pico HMAC abstraction.
- `src/security/envelope.c` and `key_wrap.c`: 512-byte four-slot header, device
  binding, fresh salt/shares, AES/Camellia key wrap and complete outer HMAC.
  Small generic Nettle C adaptation retained with upstream source/licences.
- `src/security/vault.c`: create, unlock, session I/O/lock, credential change,
  header repair and attempt/limit state machine over trusted device callbacks.
- `src/security/journal.c`, `enrollment.c` and the RP2354 authority adapter now
  provide authenticated flash records and guarded OTP lifecycle operations.
  Physical verification is pending; host failure-injection tests pass.
- Tests: independent fixed envelope/KDF vectors, malformed inputs and all-byte
  header tampering, authenticated unwrap failure, random-source errors, file-backed
  create/write/lock/unlock/rewrap, old-header rejection, payload preservation,
  final-attempt success, interrupted attempts/destruction retries, and injected
  SD/atomic-state commit failures. Software and mocked Pico SHA paths are covered.

See [the security module README](../src/security/README.md) for APIs, buffer
ownership, validation and remaining limits. The benchmark firmware still uses
public test keys: ARM compilation of this library does not activate production
unlock, persistent flash state, Secure RAM isolation or OTP programming.
