# Device authority and destruction — review proposal

**Historical proposal. Superseded for development allocation, flash encoding and
invalidation by [OTP enrollment and flash authority](v2-persistent-authority.md).**
That implementation uses raw all-ones token invalidation plus a revocation marker;
permanent page-lock policy remains production work. The review/authorization gates
below describe the earlier proposal, not a new permission requirement. Physical
OTP programming and power-cycle verification are still pending.

## Minimum authority

One enrolled vault at a time. Device-local state records device ID, volume ID,
credential_generation, exact committed-header SHA-256, authentication policy,
charged-attempt count, in-flight attempt marker, lifecycle state, enrollment-token
slot and journal sequence. Policy is duplicated locally to enforce a pending
limit action even when the SD is absent; it must exactly match the anchored header
on open. There is no file/object generation or data hash.

Root is a random protected device-local secret. Propose one additional random
32-byte **enrollment token**, in its own independently restrictable OTP page,
required alongside the root for credential binding. Never persist a root-derived
replacement, token copy or cached vault-binding value on flash/SD. RAM copies
exist only as needed and are zeroed. Destroying access to this token invalidates
all historical wrappers for that enrollment, without erasing the root or exposing
any VMK. A new vault needs a new token/page and volume ID. Exhausted token slots
mean reprovisioning is unavailable; do not reuse an invalidated slot.

The user also proposed programming the enrollment secret's physical OTP bits to
all ones, then using the next unused slot for a fresh enrollment. That is a separate
candidate for the adapter's irreversible invalidation operation. Neither that
sequence nor the lock-based sequence below has been validated on this board.
The portable module assumes only that the chosen adapter makes the old enrollment
binding permanently unavailable. Ordinary firmware reflashing does not demonstrate
that OTP was ever provisioned or that slot rotation exists.

## Proposed irreversible mechanism

Advance the enrollment-token page's **permanent hardware Secure and Non-secure
OTP access locks to inaccessible**, including the bootloader policy. The page
must contain only that enrollment's material; never a shared root or boot policy.
The [RP2350 datasheet, chapter 13](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
describes OTP page lock levels that can advance irreversibly, with redundant lock
encoding; raw/ECC reads are subject to permissions. Bootloader-only lock bits are
not a substitute for the hardware Secure lock.

This is irreversible invalidation of device-side access to a required secret,
not physical erasure of OTP bits. It satisfies the requested invalidation route
within the device's hardware/firmware trust boundary; it is not a claim against
decapsulation or an exploitable hardware permission bypass. If actual destruction
of stored bits rather than irreversible access denial is required, that is a
different hardware requirement, not something to disguise with a flash erase.

Platform review must establish an executable lock-programming sequence: legal
transitions, redundant copies, self-protection of lock words, reload/reset effects,
raw/ECC/SBPI/debug/boot access, exact silicon revision/errata, and verification after
partial power loss. Do not assume a read-only token page can later have its own
write-protected permanent lock advanced. Provisioning permissions must permit the
reviewed destruction sequence while restricting every untrusted access path.
No numeric OTP addresses or untested lock writes are authorized by this proposal.

After the final failed/interrupted attempt: zero session/candidate/binding material,
block all root/token operations for that enrollment, durably record DESTROY_PENDING,
apply the irreversible lock operation, verify it, then commit DESTROYED. If writing
DESTROY_PENDING fails, the precharged final-attempt record still requires destruction
on restart. A lock failure leaves destruction pending and the device inaccessible;
never claim successful irreversible destruction until verified. It resumes before
any unlock/provisioning attempt, with or without SD. No system can finish a hardware
write while power is absent; persistence ensures that reconnect does not buy access.

## Attempt and policy state machine

- READY: active anchor, count < max_attempts, no pending evaluation.
- ATTEMPT_PENDING: count incremented and durable before any secret evaluation.
- LIMIT_PENDING: a final failed attempt, or unresolved final pending attempt on boot.
- LOCKED: persistent lockout action; no secret evaluation until a separately agreed
  authorized recovery route exists. Initial implementation supplies no bypass/reset.
- DESTROY_PENDING / DESTROYED: as above.

On boot, an unfinished nonfinal attempt remains charged and its pending marker is
cleared durably before another attempt. At the limit an unfinished attempt invokes
the selected action. The tenth attempt can succeed, but power loss on that attempt
is conservatively failure. Correct authentication whose success-reset cannot be
committed also publishes no session; a restart may trigger destruction. This
availability tradeoff follows charging before evaluation and must be tested.

A successful authentication durably resets count/pending state. Invalid/unanchored
media does not evaluate credentials or reset counts. At count >= max_attempts,
no further KDF invocation occurs. Updating policy needs fresh authentication and
explicit physical confirmation for weakening; the authenticated host data path
has no authority to do it. Policy changes advance credential_generation through
the same two-header transaction. Reject a requested new limit that is inconsistent
with current attempt state; policy change is not an unauthenticated counter reset.

## Policy representation (portable codec implemented, still review-stage)

16 bytes, little-endian; embedded in the authenticated/anchored envelope:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 2 | Policy version = 1 |
| 2 | 2 | Policy bytes = 16 |
| 4 | 4 | max_attempts, nonzero; no unlimited sentinel |
| 8 | 2 | limit_action: 1 destruction, 2 persistent lockout |
| 10 | 6 | Reserved zero |

Default is max_attempts=10, action=1. The codec supports action=2 too; this is
representation support, not an implemented lockout-recovery mechanism. IDs 3/4
are reserved for increasing delay/timed lockout and rejected now. Those actions
need a reviewed time source, restart semantics and versioned parameters later.
Device/product policy can restrict selectable finite limits further. Do not accept
unknown actions or fall back to unlimited evaluation.

## Minimal journal proposal

Two dedicated internal-flash erase units, append-only fixed-size snapshots. Suggest
256-byte record bodies plus a separate commit program unit; actual program/erase
sizes and allocation depend on the chosen flash driver. Do not rely on SD atomicity.

Proposed logical body (offsets are illustrative until device format review):

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | Magic `FV2STATE` |
| 8 | 2 | Record version = 1 |
| 10 | 2 | Body bytes = 256 |
| 12 | 4 | Lifecycle state ID |
| 16 | 8 | Journal sequence, increasing, no wrap |
| 24 | 16 | Device ID |
| 40 | 16 | Volume ID |
| 56 | 8 | credential_generation |
| 64 | 32 | Committed authentication-header SHA-256 |
| 96 | 16 | Authentication policy, exactly as anchored |
| 112 | 4 | Charged attempts |
| 116 | 4 | Pending-attempt marker, 0 or 1 |
| 120 | 4 | Enrollment-token slot |
| 124 | 100 | Reserved zero |
| 224 | 32 | HMAC over domain + body[0:224] |

A dedicated state-MAC key is derived from the device root with a distinct HKDF
label (`FV2/device-state-mac/v1`) and public device ID. It must not depend on the
destroyable token, so DESTROYED state remains verifiable. Final exact derivation
and commit-marker encoding are part of the device-format review gate. A MAC
checks corruption/tampering but does not independently prevent replay of internal
flash. The agreed attacker controls SD; protected internal firmware/access policy
and irreversible OTP state remain assumptions.

Write body → read back/verify → program separate commit unit → read back/verify →
report durable success. Select the highest committed, MAC-valid local sequence;
equal sequences with differing bodies or undecidable corruption fail closed.
A torn, uncommitted append retains the previous committed snapshot. At rollover,
erase the other unit only while a valid latest record remains in the current unit;
commit its successor before reclaiming the old unit. Corruption that could hide a
newer committed authority record must not silently roll authentication state back.
No valid state on an enrolled device means service failure, never factory defaults.

Credential transaction: preserve old header; prepare/write/read back new header;
commit one journal snapshot containing new generation/hash/policy; mirror new SD
header. Before commit old envelope wins, after commit new wins. Mirroring failure
requires repair, not anchor rollback. Counter updates never change credential_generation.

This journal's fault model and wear budget need tests/calculation against the real
flash geometry before board integration. Desktop injection can stop before/during/
after each body, commit and erase operation. Test especially final-attempt crashes,
missing SD, old SD clones, failed irreversible transitions, duplicate sequences,
corrupt records and token-slot reuse. No firmware is to burn OTP on the bench board
as part of ordinary test execution.

## Portable adapter implementation

`src/security/vault.c` implements the attempt and header transaction logic above.
`fv_device_authority` is a logical callback boundary, with sequence-checked atomic
commits and idempotent destruction. It is NOT the on-flash record format. Tests
persist a separate mock state file and inject failures before/after atomic commit;
they do not prove flash torn-write handling, journal rollover/wear or OTP permanence.
Call recovery at boot even without media. Adapters must fail on missing/corrupt
enrolled state and must not recreate EMPTY as a fallback. There is no production
adapter or enrolled-secret reset/reprovisioning bypass in this implementation.
