# Storage and responsiveness redesign — review draft

Status: proposed approach for user review, 2026-09-14. Documentation only;
firmware has not been changed to implement this design. After approval, this
document supersedes conflicting bulk-storage, unlock-latency and execution
requirements in the earlier V1 documents. Those documents currently describe
the existing prototype, not evidence that this redesign is implemented.

## Product decisions established in discussion

- Existing development vaults are disposable. Use a new format without
  migration or backward-compatible reading/writing of old vaults.
- Retain the user-selected encryption stack and mandatory Ascon-AEAD128
  encryption/authentication. Make the mandatory layer explicit in setup and
  documentation. Do not remove an algorithm to meet performance targets without
  a separately reviewed decision.
- Recover most raw capacity using preallocated, densely packed sector metadata.
- Preserve small independently authenticated data units. A small request must
  not require processing a large data region.
- Ordinary USB-storage durability is the product expectation: surprise removal
  during writes may lose or corrupt data. Do not promise old/new sector recovery
  or filesystem transaction atomicity.
- Do not maintain permanent historical copies of user-data sectors, or introduce
  a bulk-data recovery journal by default. No automatic undelete feature.
- Use spare RAM, DMA and the second core where measurements show benefit.
- Keep interaction responsive during unlock and storage activity. An honest
  activity screen and roughly 1–3 seconds to unlock are acceptable targets;
  the measured twelve-second delay is not.

## Evidence and objectives

Existing captures are in `firmware/bench-results/`. Unlock took 12.268 seconds:
PBKDF2 3.156 seconds and iterated KMAC 9.085 seconds. Storage activation took
29 ms. The viewer-off copy contains 3,852 consecutive 512-byte write callbacks
over 32.725 seconds, approximately 58.9 KiB/s. This is one run, not a general
throughput guarantee. Earlier notes record approximately 24 KiB/s and a FAT32
format lasting 15 minutes 35 seconds; the quick/full format distinction and
exact command must be captured on the next benchmark.

| Outcome | Proposed target; requires board measurement |
|---|---|
| Payload capacity | Over 90% of raw media after all reservations |
| Unlock | Approximately 1–3 seconds after confirmation, immediate activity feedback |
| Interaction | Under 50 ms input-to-visible feedback during ordinary work; measure flash pauses separately |
| Sustained writes | At least 500 KiB/s; 1 MiB/s is a stretch goal, not a promise |
| Sustained reads | Measure and report alongside writes against raw USB/SD ceilings |
| FAT32 quick format | Tens of seconds is a provisional objective, not an established prediction |
| Small random I/O | Bound cryptographic work to the requested sectors; measure latency and physical amplification |

Record selected encryption stack, firmware build, card, host, filesystem,
cluster size, formatter command and quick/full mode for each comparison.
Report both single-layer-plus-Ascon and maximum supported stack performance;
do not meet targets by silently selecting a cheaper stack. Measure host copy
completion and flush/eject completion separately. A full-volume format is a
different workload and has no tens-of-seconds requirement.

## Preallocated packed metadata

Allocate authenticated, non-overlapping domains for headers, sector metadata,
ciphertext and the separately scoped FIDO storage. The host sees only the
logical plaintext disk. The layout is arithmetic, with no per-sector heap
allocation and no startup scan of every tag.

Start with 512-byte independently encrypted/authenticated data sectors. Each
logical address maps to one 512-byte ciphertext sector and one fixed-size
metadata entry. Pack entries into physical 512-byte metadata sectors.

Illustrative sizing, not the final serialization:

- Ascon uses a 16-byte tag; a stored 16-byte nonce makes a 32-byte entry.
- Sixteen entries fit in one metadata sector.
- Sixteen data sectors plus one metadata sector yield 16/17 = 94.12% payload
  efficiency before other reservations, initialization state and rounding.
- A tag-only 16-byte layout would yield 32/33 = 96.97%, but is valid only if a
  correct nonce scheme supplies the missing information. Do not assume it does.
- For an available domain of A physical sectors and 32-byte entries, choose the
  largest D satisfying D + ceil(D/16) <= A; authenticate all offsets and lengths.

The final entry must provide the information needed by both Ascon and selected
inner layers. Current generation/epoch/counter fields cannot simply be deleted
without replacing the associated derivations. Therefore 32 bytes is a candidate
budget, not a frozen format or a claim that only the tag needs storing.

A cold one-sector read should fetch its ciphertext and the containing metadata
sector, authenticate/decrypt that one payload, and return it. Metadata caching
can eliminate repeated metadata reads. Reading sixteen entries does not require
decrypting sixteen payloads. Contiguous host requests may be batched; random
requests must not wait for a batch to fill.

A complete-sector overwrite must not read/decrypt its previous payload. It may
read the containing metadata sector to preserve neighboring entries, then write
the new ciphertext and updated metadata. Partial USB callbacks require bounded
assembly or read/modify/write of only the affected logical sector. Larger USB
buffers must be supported throughout the adapter, not just in a configuration
constant.

Use a dedicated metadata region as the initial layout candidate. Benchmark
alternation between it and the data region; local interleaving of packed
metadata groups is an alternative only if measurement justifies it. Both retain
fixed addressing and small independent cryptographic units.

## Durability, errors and deletion

Remove permanent dual user-data slots and routine cryptographic readback of
successful data writes. Keep SD protocol CRC/status/error checks. Initially
complete a host write only after the data and relevant metadata writes report
completion from the backend. No early success for data held only in RAM.
Honor SYNCHRONIZE CACHE and eject ordering. The card's internal power-loss
behavior is outside any firmware guarantee.

Power loss between ciphertext and metadata writes may leave an authentication
failure. A torn shared metadata sector may affect multiple entries; document
and test that failure scope. Do not claim ordinary USB devices have a universal
sector-atomicity guarantee. Do not add disk-wide duplicate storage to solve this.

Return no unauthenticated plaintext. Prefer a localized medium error for an
isolated unreadable data sector, allowing unaffected sectors to remain usable.
Loss of media identity, session state, keys, or reliable transport still stops
requests and clears the session. A damaged filesystem metadata sector can affect
many files even if the encrypted adapter's failure is localized.

Firmware will not deliberately retain previous versions of overwritten payload
sectors for recovery. Ordinary filesystem deletion is not secure erasure: the
host may only mark a file's sectors free without overwriting them. The SD
controller may also retain obsolete physical pages through wear leveling.
Do not advertise secure per-file deletion or forensic removal. This proposal
does not add filesystem parsing, secure-delete commands or an undelete facility.

Durable attempt reservation, revocation and critical credential/device state
remain separate requirements. A reset must not restore spent password attempts
or cause nonce reuse. Small critical-state redundancy does not authorize storing
historical copies of bulk user data.

## Nonces and initialization: resolve before format implementation

Specify and review nonce uniqueness across overwrite, restart, interruption and
media rollback, and domain separation for each selected layer. Ordinary data
loss is acceptable; weakening confidentiality through repeated keystream is not.
Do not derive a reused nonce solely from the logical sector address, and do not
trust an attacker-replayable on-card counter as the only freshness source.

Define how never-written sectors are represented and authenticated. Arbitrarily
zeroing a tag must not silently convert corrupted written data into accepted
zero plaintext. Measure setup separately from host formatting; do not hide a
full-card initialization pass outside the quick-format benchmark. This is a
required engineering decision, not permission to create another bulk journal.
The current format does not provide general protection against replay of old
valid data; this proposal does not add that guarantee.

## Two-core ownership and RAM

Core 0 owns UI/application state and TinyUSB. Core 1 owns expensive
authentication, storage crypto, SD requests and persistent security operations.
Use bounded queues and explicitly owned buffers. Keep TinyUSB calls on its
owning core; completion messages return there. Core 0 never synchronously waits
for a long worker request. Check local TinyUSB deferred-I/O support and make any
dependency update explicit.

Use prepared cipher contexts, a bounded metadata cache and multiple transfer
buffers. Start by measuring candidate 4–16 KiB transfer buffers; these are batch
sizes, not cryptographic units. Budget both stacks, display/FIDO memory, DMA
buffers, queues and heap with measured high-water marks. Assign IRQs, DMA/PIO
resources and the SHA accelerator to clear owners. Do not use volatile alone
as inter-core synchronization. Coordinate internal flash operations through SDK
flash safety; measure the resulting pauses.

Lock stops new requests immediately, cancels queued work, resolves the active
operation, clears keys/plaintext/caches on their owners, and reports locked only
after acknowledgment. Session identifiers prevent stale completions from a
previous unlock from affecting a new session. Render actual stages or an activity
indicator; do not invent percentage progress.

## Password hardening

Retain device-root binding and durable attempt enforcement. Keep KMAC's key,
nonce and authentication roles distinct from the expensive iterated password
construction. Do not equate device unlock time with attacker guess time.

Review and calibrate the whole credential construction against the 1–3 second
budget. An SD-only attacker lacks roots; an attacker with intact attempt controls
has bounded guesses; root extraction or bypass changes the need for offline
hardening. In the existing nested envelope, Root B and the outer Ascon tag let an
attacker verify password candidates without paying the PBKDF2 branch each time.
Do not sum or multiply branch costs as a claimed attacker lower bound.

Select concrete KDF construction/parameters with attack-cost reasoning and
target measurements before changing crypto code. Spare RAM may support a
memory-hard construction, but this document does not silently select one or
change the dual-family requirement. Existing vault compatibility is not a factor.

## Implementation sequence after review

1. Measure raw SD batch I/O, RAM-backed USB throughput, individual crypto costs
   and memory headroom on the actual board. No valuable data on test media.
2. Finalize metadata serialization, nonce/inner-layer derivations, virgin-sector
   handling and credential parameters in a concrete format specification.
3. Implement the two-core request boundary and new block format, with small-I/O
   support, prepared state and batched transfers. Remove legacy bulk-copy logic.
4. Validate crypto vectors, address substitution, tag corruption, nonce behavior,
   bounds and teardown. Fault tests assert no plaintext on authentication failure
   and correct error/completion semantics, not old/new user-sector recovery.
5. Test random small operations, format, mount, sustained copies, flush/eject,
   removal during writes and UI latency. Verify no deliberate historical payload
   copies remain. Independently retain attempt-counter/critical-state tests.
6. Replace conflicting normative documents and update readiness evidence to the
   implemented behavior and measured results. Do not label targets as achieved.

## Review boundary

No further product clarification is required to review this approach. Remaining
items are bounded engineering investigations listed above, not reasons to retain
the old format. This draft does not authorize an unannounced new cipher, bulk
recovery journal, early write acknowledgment or stronger deletion promise.
Implementation follows user review of the documented approach.
