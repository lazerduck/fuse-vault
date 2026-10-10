# FIDO journal storage (format 3)

The firmware uses `fv_fido_journal` with bounded engine read/write callbacks.
The legacy snapshot API below remains available for migration fixtures and older
host tests. It is not the firmware storage path.

## Fixed physical geometry

All sectors are 512 bytes. The reservation remains physical sectors 16–2063.

| Physical sectors | Contents | Size |
| --- | --- | --- |
| 16–125 | 110 eager sector-authentication metadata sectors | 55 KiB |
| 126–1773 | 1648 encrypted filesystem sectors | 824 KiB |
| 1774–2029 | 128 two-sector journal entries | 128 KiB |
| 2030–2063 | Control/migration area | 17 KiB |

Normal control records alternate at physical 2062 and 2063. The rest of the
control area supports one-time migration and is reserved afterwards. Headers
0–15 and USB metadata/data at 2064 onward are never written by this store.

The filesystem keeps its linked records, 32-bit logical addresses, 16 KiB
permanent-record pool and allocator. Runtime storage has eight cached plaintext
sectors, at most 128 journal destination indices and function-scoped record views
capped at 4096 bytes. The existing file/RP indexes have fixed bounds for 512 slots.
There is no complete plaintext image or per-sector fingerprint table in firmware.

## Keys and journal records

HKDF retains the legacy fixed-width context construction: purpose including NUL,
SHA-256 of the volume descriptor, bank zero, and layer. New purposes are
`FV3/fido-xts`, `FV3/fido-sector` and `FV3/fido-journal`. Engine wrapping retains
`FV2/fido-engine/v1`, preserving resident and nonresident credentials. All
cipher/HMAC contexts, cache entries and temporary record views are wiped on close.

A journal entry consists of a 512-byte header and a 512-byte replacement sector.
The header contains `FV3JREC`, the transaction sequence, entry index and relative
home-sector destination. Its HMAC covers the first 480 header bytes and the whole
replacement sector. Destinations must be unique and inside the 1758-sector home
region. Replacement data is already encrypted with the home encryption stack;
authentication metadata contains tags, not plaintext credential records.

Control records contain `FV3JCTL`, version 3, monotonic sequence, state, entry
count, legacy source geometry, reset policy and transaction/source digest. Their
HMAC covers the first 480 bytes. Sequence parity selects the control slot.
States are CLEAN, READY, RESET and MIGRATE. Sequence overflow fails closed.

## Transaction and recovery ordering

Writes update journal entries through a block-device overlay beneath the existing
sector-authentication layer. Reads see their transaction's pending replacements.
The main filesystem stays unchanged until the complete journal is committed.
Repeated writes to a sector reuse its entry. The 128-entry bound includes data
and authentication metadata; overflow faults the session and requires reopen.

1. Synchronize journal entries, read back/authenticate all entries, and calculate
   their ordered digest.
2. Publish READY with that digest; synchronize and read back the control record.
3. Validate the complete committed journal before applying any destination.
4. Apply replacements, synchronize, and compare destination readback with them.
5. Publish CLEAN; synchronize and read back before journal reuse or success.

Open recovers READY before exposing the engine. Replaying final sector images is
idempotent. Read/I/O errors are not absence; invalid committed entry data fails
closed. Uncommitted journal contents may be ignored. A failed commit can still
have become durable; an error is not evidence that the credential was never saved.
All operations require serialized ownership and the original unlocked authority,
credential generation and volume descriptor. Cancellation before publication may
abort; cancellation cannot undo a published transaction.

The transaction digest authenticates the temporary recovery operation. It is not
a table of persistent sector fingerprints. Old authentic home-sector/tag pairs
and complete SD rollback remain outside the freshness guarantee. Media must be
reopened after replacement; coherent external mutation is unsupported.

## Destructive reset

After UI authorization, publish and verify RESET, discard pending writes, then
initialize home sectors directly. No copies of erased credentials are journaled.
RESET contains the host-reset UV policy to retain, or -1 for a local reset.
Reopening repeats interrupted initialization before exposing the filesystem.
CLEAN is published only after initialization is synchronized and authenticated
from media again. Engine initialization
creates replacement FIDO secrets through a normal transaction. Power loss between
these stages exposes no old credentials; initialization can safely run again.
This is logical reset, not guaranteed physical erasure of SD controller copies.

## Automatic startup migration

Migration runs on first FIDO access after successful unlock, when the VMK is
available. Authenticate the newest legacy snapshot and its complete digest first.
Copy its contiguous manifest, metadata and ciphertext to physical sector 1774
onward. A 128 KiB snapshot consumes 275 sectors, ending at 2048; this is outside
both old banks' used ranges and outside the new home region. Authenticate the
staged copy and publish MIGRATE at the new control location before modifying home.

Initialize the enlarged home image, stream the old bytes to its upper end, validate
both old linked lists and relocate their next/previous pointers. Record payloads
and engine secrets remain unchanged. Authenticate destination sectors, publish
CLEAN, and supersede MIGRATE before allowing the staging area to serve as a journal.
Interrupted migration resumes from its staged authenticated source. Both legacy
64 KiB and 128 KiB snapshots are supported; malformed lists fail closed.
Downgrade is unsupported. Missing/corrupt storage never triggers an implicit reset.

## Capacity and validation

Resident slots are 0–511. Existing marker IDs remain for slots below 256;
extended markers use 0xA000–0xA0FF and carry the high slot byte in marker version 2.
Object IDs for extended slots and formerly unusable slot 32 use a disjoint range
starting at 0x2000. This avoids the old 0xE020 platform-file collision. The legacy
RP namespace remains 256 entries; native RP enumeration supports all 512 slots.

`fido_journal_reference` uses python-fido2 for signature, migration, 512-slot,
full-store, delete/reuse, reset and verification-policy checks.
`fido_journal_interruptions` injects every write/sync failure in reset and both
migration sizes, every write/sync/read failure in a multi-sector transaction,
torn writes, lost unsynchronized data, repeated recovery failures and corruption.
Validation on 2026-10-10: the normal desktop suite passed 43/43 tests. The
firmware-adapter, journal-reference and journal-interruption tests also passed
3/3 under AddressSanitizer and UndefinedBehaviorSanitizer (leak detection disabled).
The
interruption harness exercised 14 writes, 4 syncs and 46 reads in its ordinary
transaction; 439 writes/4 syncs for reset; 1227 writes/7 syncs for 128 KiB migration;
and 834 writes/7 syncs for 64 KiB migration. Counts are operation boundaries, not
claims about every possible SD-controller failure. The 512-slot test also lists
all 512 RPs and verifies keys around the old slot boundary and remapped slot 32.

The RP2354 firmware build succeeds with 343532 bytes of BSS and a linker heap
range of 160620 bytes (about 157 KiB) before live allocations. This is not a peak
RAM or stack high-water measurement, and no device was flashed during validation.

Hardware power-loss, latency and peak live RAM acceptance remain outstanding.
Guarantees assume the block adapter honours completed synchronization and failed
writes do not destroy unrelated previously durable sectors.

---

# Historical formats 1 and 2 (migration input only)


The F2 store connects the portable FIDO engine to the existing V2 encrypted block
pipeline. It does not attach HID or implement physical UI. See the
[delivery ledger](v2-fido-progress.md) for remaining stages.

## Ownership and lifecycle

`fv_fido_store` holds a reference to an already unlocked `fv_vault`, the original
volume descriptor/credential generation, and public snapshot bookkeeping. It
retains no derived keys. Every operation checks that the vault is unlocked, media
is present, and internal authority is ACTIVE, not attempt-pending, and matches the
session's device/volume/token/credential generation. Cipher/HMAC contexts and
scratch buffers are wiped after each operation. The store does not write internal
authority or spend OTP slots per snapshot.

The firmware worker must exclusively serialize store, engine and vault use. Close
the engine (wiping its caller-owned plaintext image and cached keys), then close
the store before vault lock, credential change, media replacement or route change.
F3 must enforce that lifecycle; a pointer to an unlocked session alone is not a
replacement for USB/session invalidation. Do not use raw store APIs concurrently
with a live engine. Hot media mutation is unsupported.

- `fv_fido_store_open`: read-only recovery; never initializes. Missing or invalid
  commit records return CORRUPT, not an empty image. Every failed read clears the
  entire 128 KiB output and leaves the store closed.
- `fv_fido_store_initialize`: explicitly destructive trusted-setup operation,
  requiring an unlocked vault and `confirmed=true`. Invalidates both banks and
  commits an all-FF initial engine image. This boolean is an internal caller
  contract, not proof of user authorization; the trusted UI supplies it for
  **Reset FIDO**. Never call automatically after open failure. Can erase existing passkeys.
- `fv_fido_store_prepare_new`: creation-only automatic initialization. Requires
  EMPTY authority and the private generation-one creation session; writes the
  initial image before the vault becomes ACTIVE, then wipes its workspace.
  New device-UI vaults invoke this after creating their encrypted FAT32 filesystem.
  Failure leaves setup retryable. Existing vaults are rejected, even when their
  FIDO data is missing or corrupt.
- `fv_fido_store_engine_key`: derives the engine wrapping key after successful
  open/initialization. Wipe the temporary key after `fv_fido_engine_open`.
- `fv_fido_store_commit`: persist the complete engine image without modifying its
  input. Reject stale owners, changed authorization, and generation overflow.
  Failures fault the store. A failed/uncertain final write can still be durable;
  close/reopen before any retry. Do not interpret an error as proof of no change.

Connect the engine's durable commit callback to `fv_fido_store_commit`. The
file-backed test peer does this with the real vault lifecycle and independently
verifies signatures using python-fido2. It uses test-only entropy/UI/authority;
none of those fixture callbacks are part of firmware.

## Region and bank layout

Physical SD sectors 16–2063 remain private. Headers at 0–15 and USB metadata/data
at 2064 onward are untouched. Existing eager/lazy USB formats are unchanged.
There is no automatic migration on flash or vault unlock.

| Bank-relative sectors | Contents |
| --- | --- |
| 0 | 512-byte authenticated commit manifest |
| 1–18 | Existing V2 sector-HMAC metadata for 256 logical sectors |
| 19–274 | 128 KiB encrypted engine image |
| 275–1023 | Reserved, untouched |

Bank 0 starts at physical sector 16; bank 1 starts at 1040. Both use the existing
eager `fv_auth_store` layout (no lazy bitmap is necessary for a full snapshot).
XTS sector inputs are logical 0–255, with independent keys for each bank.

## Expansion from the 64 KiB format

New snapshots use version 2 and a 128 KiB image. Version 1 snapshots retain their
original 9 metadata sectors and 128 payload sectors. The reader authenticates
both formats and selects the newest generation across both banks. It verifies
the original ciphertext digest before expanding a legacy image in RAM.

The legacy engine uses two linked file regions with 32-bit logical addresses.
Expansion validates decreasing links, reciprocal previous links and record bounds,
then moves the complete old image upward by 64 KiB and adjusts file-list links.
Credential/object payloads remain unchanged, and new free space is filled with FF.
A wholly FF explicitly initialized image is supported; malformed lists fail closed.

Opening remains read-only. The next engine commit writes version 2 to the other
bank, leaving the previous valid snapshot intact until a later commit reuses it.
No FIDO reset, re-enrollment, USB geometry change or enlarged disk reservation is
required. **Do not downgrade to 64 KiB firmware after new snapshots are saved:**
it cannot read version 2 and may select a stale surviving version 1 snapshot.
Forward migration is supported; downgrade migration is not.

## Derivation and authentication

Use HKDF-SHA-256 extract with volume ID as salt and the unlocked VMK as input.
Expand context is `purpose including NUL || SHA256(canonical 128-byte volume
 descriptor) || bank u32-LE || layer u32-LE`. Purposes are:

- `FV2/fido-xts/v1`: 64 bytes per active layer/bank, same cipher IDs/order as USB.
- `FV2/fido-sector-hmac/v1`: 32 bytes per bank for existing ciphertext sector tags.
- `FV2/fido-manifest/v1`: 32 bytes per bank for the commit record.
- `FV2/fido-engine/v1`: 32 bytes, bank/layer zero, for pico-fido key wrapping.

These domains differ from every USB working-key purpose. Credential rewrap does
not alter the volume descriptor or VMK, so passkeys and engine keys survive an
unlock-method change. Vault recreation uses a new VMK/volume ID. Internal key
revocation prevents unlocking both USB and FIDO even after old SD restoration.

The manifest is canonical, little-endian, with all reserved bytes zero:

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 8 | `FV2FIDO` plus NUL |
| 8 | 4 | format version 2 (legacy 1 accepted on read) |
| 12 | 4 | bank index |
| 16 | 8 | nonzero snapshot generation |
| 24 | 4 | image size 131072 (legacy 65536) |
| 28 | 4 | reserved |
| 32 | 16 | volume ID |
| 48 | 32 | SHA-256 of all encrypted payload sectors in order |
| 80 | 32 | SHA-256 of volume descriptor |
| 112 | 368 | reserved |
| 480 | 32 | HMAC-SHA-256 over `FV2/fido-commit/v1` including NUL, then bytes 0–479 |

Per-sector HMAC protects ciphertext and logical position. The authenticated
whole-image digest additionally rejects mixing old valid sectors/tags under a
new manifest. No plaintext-image fingerprint is stored externally.

## Commit and recovery

1. Re-read both manifests and reject stale generation/bank/digest bookkeeping.
2. Invalidate the inactive bank's manifest; synchronize before overwriting it.
3. Initialize that bank's tag metadata; encrypt/write the image in 4 KiB batches
   using the existing pipeline and sector-HMAC code.
4. Synchronize, drop cached tags, and read back/authenticate the complete ciphertext
   image. Compare its digest with the ciphertext just generated.
5. Write the authenticated manifest last, synchronize, and verify its readback.
   Only then report success and select that bank in memory.

Opening authenticates manifests and selects the highest committed generation.
Malformed/torn manifests are not commits; I/O errors are not missing data. Equal
valid generations are rejected. If the newest authentic manifest exists but its
payload is corrupt, fail closed rather than silently use the older bank. Written
markers changed to unset also fail; a FIDO snapshot must contain all sectors in the selected image.

Injected interrupted writes recover the old or new complete snapshot, never a
mixture. Initialization failure can leave no valid snapshot and requires explicit
setup retry. These guarantees assume the block adapter honors completed syncs and
failure is confined to the requested writes. Real SD/NAND controller power-loss
behavior still needs physical testing; these tests are not hardware guarantees.

## Accepted replay policy

There is no internal anti-rollback anchor. Replaying an entire old authentic bank
or SD image on the same enrolled device is allowed. An attacker controlling
storage can also remove a newer manifest and expose an older valid snapshot.
That can undo FIDO deletion/reset, but cannot restore internal attempt budgets or
revoked vault authority. Soldered NAND reduces access; it does not provide freshness.
No passkey export/backup flow is implemented. Signature counters remain zero.
