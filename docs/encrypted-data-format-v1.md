# Fuse Vault encrypted block format V1

Status: portable prototype, 2026-09-05. This format is not yet wired to SDIO,
USB MSC, a filesystem, or production provisioning.

## Scope and threat boundary

V1 turns an untrusted 512-byte block device into a smaller 512-byte logical
device. It provides confidentiality, authenticated logical addressing and vault
identity, detection of torn writes, and single-logical-sector old/new recovery.
The adapter accepts only one logical block per read or write so it does not claim
atomicity for a multi-sector filesystem operation.

The removable medium may corrupt, omit, reorder, or substitute bytes and blocks.
It holds no secret. An attacker can still deny service. V1 does not detect an
attacker restoring both valid copies of an older sector; rollback freshness
beyond the interrupted current sector update requires a future authenticated
root or epoch in trusted internal storage. That limitation does not cause nonce
reuse: every adapter session obtains a fresh 128-bit random epoch before it can
write, and callers must never deliberately replay an epoch after reset or media
rollback.

## Geometry and canonical record

Each logical 512-byte sector owns two 1,024-byte copy-on-write slots, consuming
four physical blocks. Raw media size must be a non-zero multiple of four blocks.
The exposed capacity is `floor(raw_blocks / 4)`, with no hidden rounding, so
payload capacity is 25% and overhead is 75%. The Stage 1 header blocks are outside
this geometry and must be reserved by the eventual volume-layout layer.

All integers are unsigned little-endian. Reserved fields and bytes must be zero.
Native C layout is never persisted.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 8 | `FVDATA1\0` magic |
| 8 | 2 | format version, 1 |
| 10 | 2 | authenticated-header length, 88 |
| 12 | 2 | used-record length, 616 |
| 14 | 2 | reserved zero |
| 16 | 8 | logical block number |
| 24 | 8 | generation, starting at 1 |
| 32 | 16 | random writer-session epoch |
| 48 | 8 | monotonically consumed session write counter, starting at 1 |
| 56 | 16 | derived ASCON public nonce |
| 72 | 16 | vault ID |
| 88 | 512 | ciphertext |
| 600 | 16 | ASCON-AEAD128 tag |
| 616 | 408 | canonical zero padding |

The complete 88-byte header is associated data. Consequently the tag binds the
format fields, logical address, generation, epoch, counter, nonce, and vault ID
to the ciphertext. A record is released as plaintext only after canonical checks,
nonce re-derivation, constant-time nonce/vault comparison, and successful AEAD
verification. Caller output is zeroed on every integrity or I/O failure.

## Key and nonce derivation

The 256-bit volume master key is never used directly by ASCON. The adapter
derives independent keys bound to the vault ID:

- 128-bit encryption key: HMAC-SHA-256 with label
  `fuse-vault/v1/data/ascon-aead128/key`, truncated to 128 bits.
- 256-bit nonce-derivation key: KMAC256 with customization
  `fuse-vault/v1/data/ascon-aead128/nonce-key`.
- 128-bit public nonce: KMAC256 under the nonce key with customization
  `fuse-vault/v1/data/ascon-aead128/nonce` over canonical vault ID, logical
  address, generation, session epoch, and counter.

The credential envelope, header authentication, journal authentication, data
encryption, and nonce derivation therefore use distinct domain strings and keys.
Within a live session, the counter is consumed before the first untrusted write
and is never rolled back after an error. Across lock, fault, restart, or rollback,
a newly sampled epoch changes every subsequent nonce. Generation or counter
exhaustion fails closed. The 128-bit random-epoch collision probability is the
residual V1 nonce-collision bound; production initialization must use the
RP2354 TRNG health-checked random service.

## Commit and recovery

Generation 1 uses slot 0 and each later generation alternates slots. Updating a
sector performs: read/authenticate both slots; consume a session counter; build
and encrypt the next generation in a plaintext/ciphertext workspace; write the
inactive two-block slot; synchronize the underlying device; read it back; and
authenticate/compare it. The old slot is never modified during this transaction.

At recovery, an erased pair reads as a logical zero sector. One valid slot is
accepted even if the other is erased, torn, or invalid. Two valid slots are
accepted only when their generations are consecutive, and the newer is selected.
Two invalid slots or valid non-consecutive generations fail with integrity error.
Thus every byte cut in a replacement write recovers the old authenticated value
or the fully authenticated new value. A returned write error is deliberately
ambiguous; callers must read after restart rather than assume which value won.

## Locking, failures, and resource budget

`fv_encrypted_block_lock` and `fv_encrypted_block_fault` clear the entire session
object through volatile stores: both derived keys, vault ID, random epoch,
counter, backend pointer, and readiness flag. It then restores only an inert
public adapter interface so subsequent calls return not-ready. Local
plaintext, decoded-record, key-derivation, nonce, and serialized-record
workspaces are cleared on all exits. The adapter returns not-ready once locked;
through that interface.

The persistent cost is four raw blocks per logical block. The session object is
small (roughly 120 bytes, ABI dependent), uses no heap, and the worst operation
uses roughly 3 KiB of stack due to the 1 KiB canonical record and two decoded
candidate workspaces. This fits RP2354 SRAM but must be placed on a deliberately
sized storage-task stack; firmware link/map and target stack-watermark evidence
remain Stage 5 gates. A read performs two 1 KiB physical reads and up to two
ASCON authentications. A write additionally performs one 1 KiB write, a sync,
and a 1 KiB verified readback. This prioritizes simple recovery over capacity and
throughput. Target ASCON timing, SD latency, stack watermark, and USB throughput
must be measured before production integration.

## Verification evidence

`fuse_vault_encrypted_block` contains deterministic golden values for both
derived keys, the nonce, and a digest of the canonical ciphertext/tag record;
metadata/ciphertext/tag/padding corruption checks; logical-block and vault
substitution; truncation; no-plaintext-on-error and teardown checks; persistence
over the host file device; a 1,000-operation randomized reference model across
restarts; and all 1,025 possible byte prefixes of a 1,024-byte replacement write.
