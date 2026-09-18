# Portable vault and security module

Implements the [volume/key design](../../docs/v2-volume-and-key-design.md) and
[four-slot VMK envelope](../../docs/v2-vmk-envelope-proposal.md). The format is
implemented for development/testing, not frozen for production use.

## Available now

- Canonical 128-byte immutable descriptor and 512-byte authenticated header.
  One to four active cipher/share slots, followed by zeroed unused slots.
  Cipher order and repeated families are supported; every slot gets distinct keys.
- Random 32-byte VMK; n-of-n XOR shares wrapped with AES-256-KW or Camellia-256-KW
  matching the storage layers. Separate HMAC authenticates the complete envelope
  before any unwrap. No partially recovered VMK is published on failure.
- Root + enrollment-token binding, PBKDF2-HMAC-SHA-256 hardening, and separated
  HKDF keys for each wrapper, XTS layer and sector authentication.
- Create, unlock, read/write, lock, credential change and header repair. Rewrap
  keeps the VMK/descriptor/data keys and payload unchanged; salt/shares refresh.
- Durable-attempt state machine through a trusted adapter. Default ten attempts
  with destruction, or persistent lockout; final-attempt success, interruption,
  uncertain commits and failed destruction retries are covered by tests.
- Exact header hash/generation anchored on the device. Credential changes write,
  sync and verify the other SD header before committing the new authority record.
  Mirroring follows commit; its failure never rolls the anchor back.

The small [Nettle adaptation](../../third_party/nettle_keywrap/README.md) supplies
maintained C generic key-wrap loops with Mbed TLS AES/Camellia callbacks. Its
upstream source and LGPL/GPL licences are retained. This reuse and testing do not
constitute an independent security audit of our composition or integration.

## API and ownership

See [`vault.h`](include/fuse_vault/vault.h). Zero-initialize one `fv_vault` session;
use one trusted owner, with no concurrent/reentrant calls or copied live sessions.
The platform supplies block I/O, cryptographic random bytes, trusted KDF bounds,
and device authority. The authority loads/atomically commits logical snapshots,
provides a binding key without exporting root/token, and irreversibly invalidates
an enrollment. An error from commit may mean the new snapshot is already durable:
the caller remains locked and reloads authority on the next operation.

`fv_vault_recover()` must run at boot even without an SD. Interrupted final attempts
complete the selected limit action before unlock/create can proceed. Missing or
corrupt device state must fail; never convert it to an empty/factory-default state.
Creation requires an explicitly pre-provisioned EMPTY enrollment and formats SD
metadata. It returns locked. Production provisioning/slot allocation is separate.

`fv_vault_unlock()` charges before credential evaluation and commits success before
publishing a session. Only exact locally anchored headers can be selected; SD
copies do not vote for a highest generation. Bad/missing headers don't consume a
guess. Authentication failures do; errors after a durable charge retain that charge.
The tenth attempt may succeed. Failure to commit its success can destroy the vault
on restart, a deliberate availability tradeoff of conservative accounting.

Reads authenticate the complete requested batch before decrypting any of it.
Unset sectors remain zero. Writes encrypt in place, write data/metadata and sync
before success; the caller's valid write buffer is cleared on success and failure.
Failed reads clear their valid output buffer. Requests are 1–64 whole sectors,
four-byte aligned. Storage corruption remains a local read failure; transport
failure/removal locks and clears the session. Media-removal/UI/USB callbacks must
also call `fv_vault_lock()` promptly, including when no I/O is in flight. The caller
owns and must erase credential buffers and any other plaintext copies.

Credential change requires the current secret again, even when unlocked. Policy
changes also require confirmation from the trusted on-device UI. It locks on every
exit. `FV_VAULT_CHANGED_NEEDS_MIRROR` means the NEW credential has committed; unlock
with it and call `fv_vault_repair_headers()` to restore the other copy. Other
uncertain state-write failures require reloading the durable authority to determine
which credential won. There is no host command exposing any of these operations.

Low-level `envelope.h` functions deliberately do not enforce attempts/anchoring;
only the trusted lifecycle/adapter may use them. No USB raw unwrap/VMK export API.
Text profile 1 uses exact bytes from a validating UTF-8 UI, with no normalization;
profile 2 accepts D-pad symbols 1–5. All secret inputs are 1–256 bytes.

## Validation and remaining platform work

Desktop tests use a file-backed SD and separate mock state file, with deterministic
**test-only** randomness and small test KDF counts. State-file structs are a test
fixture, NOT the proposed flash serialization. Fault injection models atomic
commits both before and after durability, not physical flash tears/wear/erase timing.

Independent fixed envelope fixtures cover one, two, three and four layers,
including repeated families. [`tests/envelope_reference.py`](../../tests/envelope_reference.py)
uses Python hashlib/hmac and cryptography/OpenSSL (tested with cryptography 41.0.7):
AES uses its key-wrap implementation, Camellia uses an independent RFC loop over
its ECB backend. Fixture regeneration is optional; normal builds use checked-in
vectors. Tests cover every header byte's tampering, authenticated bad KW, wrong
credential/binding, output clearing, cost/profile/capacity bounds, and RNG failure.
Lifecycle tests cover payload preservation, replayed old headers, final-attempt
success/destruction, retry after failed destruction, header/commit failures, and
no plaintext release from a partly corrupted batch. SHA-adapter versions exercise
the same APIs against the mocked Pico accelerator contract.

No heap allocation in this module. Session/scratch objects still require an
explicit board RAM/stack budget before runtime integration; default SDK thread
stacks must not be assumed sufficient. ARM compilation alone isn't a runtime test.

The TRNG/CTR-DRBG adapter, 60,000-iteration enrollment profile, authenticated flash
journal and fixed OTP root/token lifecycle are implemented. See
[persistent authority](../../docs/v2-persistent-authority.md) for encoding,
allocation, recovery limits and pending physical tests. Production access/debug
protections, Secure RAM isolation, on-device credential UI and USB MSC remain on
the [release checklist](../../docs/production-checklist.md). The separate benchmark
firmware still uses public test keys.

Run `cmake --build build -j4` then `ctest --test-dir build --output-on-failure`.
For the configured sanitizer build, run with `ASAN_OPTIONS=detect_leaks=0` in this
environment. Both suites and ARM library compilation are checked for this revision.

## Pico bring-up integration

The separate [security diagnostic image](../../firmware/security/README.md) links
these APIs to real SD/SHA/cipher backends. `random.c` adds Mbed TLS AES-256 CTR-DRBG
over an injected checked 192-bit entropy source; the RP2354 adapter uses normal
TRNG health checks and bounded collection. `journal.c` and `enrollment.c` implement
the persistent authority behind the same vault callbacks, with physical adapters
in `src/platform/rp2354/authority.c`. Debug commands provision, exercise and destroy
an enrollment without exporting secrets. Hardware OTP programming and power-cycle
verification remain pending; the earlier successful board tests used RAM authority.
