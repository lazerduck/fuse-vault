# V2 FIDO snapshot storage (development format 1)

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
  entire 64 KiB output and leaves the store closed.
- `fv_fido_store_initialize`: explicitly destructive trusted-setup operation,
  requiring an unlocked vault and `confirmed=true`. Invalidates both banks and
  commits an all-FF initial engine image. This boolean is an internal caller
  contract, not proof of user authorization; only the future trusted UI can supply
  it. Never call automatically after open failure. Can erase existing passkeys.
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
| 1–9 | Existing V2 sector-HMAC metadata for 128 logical sectors |
| 10–137 | 64 KiB encrypted engine image |
| 138–1023 | Reserved, untouched |

Bank 0 starts at physical sector 16; bank 1 starts at 1040. Both use the existing
eager `fv_auth_store` layout (no lazy bitmap is necessary for a full snapshot).
XTS sector inputs are logical 0–127, with independent keys for each bank.

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
| 8 | 4 | format version 1 |
| 12 | 4 | bank index |
| 16 | 8 | nonzero snapshot generation |
| 24 | 4 | image size 65536 |
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
markers changed to unset also fail; a FIDO snapshot must contain all 128 sectors.

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
