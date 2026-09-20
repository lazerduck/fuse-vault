# Lazy metadata initialization (volume layout version 2)

New vaults use a small initialization bitmap. Existing layout-version-1 volumes
remain readable/writable and retain their layout when credentials/policy change.
Flashing does not migrate, resize or erase an existing volume. Older firmware
rejects the new version; do not roll back firmware for a version-2 volume.

## Layout

All sectors are 512 bytes. Let `N` be logical data sectors, `M = ceil(N / 15)`
metadata sectors, and `B = ceil(M / 4096)` bitmap sectors.

| Area | First physical sector | Length |
| --- | ---: | ---: |
| Headers/reserved | 0 | 16 |
| Future secure objects/FIDO reservation | 16 | 2048 |
| Initialization bitmap | 2064 | B |
| Existing HMAC/state metadata | 2064 + B | M |
| Encrypted data | 2064 + B + M | N |

Capacity selection finds the greatest N satisfying `2064 + B + M + N <= physical
sectors`, capped at UINT32_MAX for USB READ(10). The FIDO reservation is unchanged.
The bitmap adds under 1 MiB on the current approximately 59 GiB physical card.

The 128-byte descriptor retains the `FV2VOL01` family magic. Little-endian fields:

| Byte offset | Bytes | Layout version 2 value |
| --- | ---: | --- |
| 8 | 2 | 2 (previously 1) |
| 48 | 8 | Metadata base: 2064 + B |
| 56 | 8 | M, unchanged meaning |
| 64 | 8 | Data base: 2064 + B + M |
| 100 | 8 | Bitmap base: 2064 |
| 108 | 8 | B |

Bytes 116–127 remain zero. Other fields and sector-tag format are unchanged.
Version-1 descriptors require bytes 100–127 zero and their original offsets.
Unknown versions and noncanonical/redundantly inconsistent layouts are rejected.
The entire descriptor remains bound into existing envelope authentication, key
derivations and flash-anchored header hash; this is not an unauthenticated switch.

## Bitmap semantics

Metadata sector `j` uses bit `j % 8` (least-significant bit first) of byte `j / 8`.
The relevant bitmap sector is `j / 4096`.

- Bit 0: synthesize 512 zero metadata bytes in RAM, without reading stale metadata
  or ciphertext. Its 15 data sectors consequently read as logical zeros and are
  not passed through XTS decryption.
- Bit 1: read existing metadata and apply normal per-sector state/HMAC checks.

Only the bitmap is cleared at creation, followed by sync. Metadata and data areas
remain untouched until used. Setup then writes the headers and commits ACTIVE
state using the existing lifecycle. Interrupted creation before that commit can
be restarted from EMPTY. There is no resumable progress record.

One 512-byte bitmap sector is cached per storage session, plus small bookkeeping.
The existing six-sector metadata cache synthesizes unset entries and batches
reads of adjacent initialized entries. Formatting reuses the existing 32 KiB
worker workspace. Progress counts bitmap sectors and uses KiB for small totals.

## First-write ordering

For a batch touching any uninitialized metadata sector:

1. Synthesize that sector as zeros; preserve existing entries in initialized ones.
2. Compute HMAC tags, write ciphertext, then write complete affected metadata sectors.
3. Sync the device before publishing ANY new initialized bit.
4. Read-modify-write the relevant bitmap sectors, preserving other bits, and sync
   each modified bitmap sector before success.

Already-initialized metadata requires no additional bitmap write or barrier.
The vault's normal write sync/acknowledgment still applies. Write, bitmap read, or
sync failures close the store; uncertain RAM state cannot be reused for later I/O.

After interruption, a bit still clear hides the unacknowledged write as empty. A
published bit points to ciphertext/metadata that passed the earlier durability
barrier. A batch crossing bitmap sectors can be partially visible; this does not
introduce atomic multi-sector write guarantees. As elsewhere, correctness relies
on the SD driver/card honoring sync. Torn/corrupt sectors can cause data loss or
integrity errors and are not claimed to be transactionally recoverable.

## Security boundary

The bitmap, like existing per-sector written/unset flags, is not encrypted or
MAC-protected. Clearing a bit can hide data, consistent with the accepted physical
attacker deletion/corruption scope. Setting a bit does not bypass HMAC verification
of written ciphertext. Metadata/data from another volume cannot be accepted as
written plaintext without a valid tag under this volume's keys and LBA binding.
Valid old ciphertext at the original LBA remains subject to the existing rollback
policy. This adds neither plaintext/key export nor a freshness guarantee.

## Validation and deployment

Tests exercise dirty preexisting SD contents, no reads of uninitialized metadata,
bitmap-sector boundaries, mixed initialized regions, preservation of unrelated
bits, and failures before/after every first-write write/sync step. Restart tests
restore only durable bytes and require either logical zeros or authenticated data.
Legacy envelope unlock/read/write/credential change is tested independently of the
new creation path. On-board timing and file round-trip remain acceptance checks.

Flash `build-pico-device-ui/v2_device_ui_bitmap.uf2`. If setup was interrupted and
the device is EMPTY, simply run setup. If an old volume is ACTIVE, it remains the
old layout: to test fast creation, unlock using its current credential, unmount,
and explicitly erase/create through the UI. That reset consumes the next OTP token
slot. The public test unlock helper is inappropriate once a D-pad credential exists.
