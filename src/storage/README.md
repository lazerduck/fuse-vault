# Authenticated sector storage (experimental layout)

`auth_store.c` adds HMAC-SHA-256 tags to the existing raw block-device interface.
It stores and returns **ciphertext**. The caller supplies a distinct integrity
key, a volume ID, a trusted layout, and a timer. Encryption remains in the crypto
pipeline; the benchmark engine authenticates an entire read batch before calling
any decryption routine. No USB, Pico runtime, allocation or password code is used.

## Layout

For N logical data sectors starting at physical base B:

- Metadata length M = ceil(N / 15).
- Metadata occupies physical sectors B through B+M-1.
- Ciphertext occupies B+M through B+M+N-1.
- Logical sector L maps to metadata sector B+floor(L/15), slot L mod 15,
  and ciphertext sector B+M+L.

Each 512-byte metadata sector contains:

| Offset | Size | Meaning |
|---|---:|---|
| 0 | 15 | One state byte per slot: 0 unset, 1 written |
| 15 | 17 | Reserved; initialized to zero, currently ignored |
| 32 | 480 | Fifteen full 32-byte HMAC tags |

This uses 15 tags rather than 16 to keep explicit state and tags in the same
physical sector. Metadata costs about 6.67% of payload size (93.75% payload
utilization before future header/private-region reservations and rounding).
A 4 MiB logical benchmark uses 547 metadata sectors: 280,064 extra bytes.
The maximum benchmark footprint is therefore 4,474,368 physical bytes.

This is an explicit experimental layout, not a self-describing final volume
format. The eventual authenticated volume header must supply and protect its
layout and volume identity. The module validates capacity/overflow at open.

## Authentication

Each tag authenticates the following fixed, unambiguous byte sequence:

1. Sixteen-byte domain `46 56 2d 53 45 43 54 4f 52 2d 4d 41 43 00 00 01`.
2. Sixteen-byte volume identifier.
3. Eight-byte little-endian logical sector address.
4. Exactly 512 ciphertext bytes, after all encryption layers.

`src/crypto/hmac.c` implements the standard HMAC construction using Mbed TLS
SHA-256, with prepared inner/outer states so key processing is not repeated for
every sector. Tag comparison scans all 32 bytes without an early mismatch exit.
Desktop uses software SHA-256. The current board build uses the Pico SHA
accelerator, selectable with `FV_HMAC_PICO`; both produce the same tags. The
[backend notes](../crypto/backends/README.md) explain ownership and self-checks.
Historical protocol-4 results measured software SHA-256 only.

## Unset, corruption and rollback

- Formatting initializes only metadata, with every sector unset. Payload sectors
  are untouched. Formatting is explicit and destructive; opening is read-only.
- An unset sector returns zeros and does not read or decrypt its payload. Its bit
  is reported in `unset_mask` so the caller skips decryption.
- Written sectors require valid HMACs. Unknown state bytes are integrity errors.
- Failure wipes the complete requested output buffer; no partially authenticated
  batch is returned. Integrity failure is a local read error, not automatic vault
  lockout. Transport failure makes the store not ready until reopened.
- Changing a written marker to unset is permitted destruction under the agreed
  threat model; it yields zeros, never the stored plaintext.
- Replaying an older valid ciphertext/tag pair at its original LBA is accepted.
  Relocation, arbitrary ciphertext/tag changes, wrong keys and volume substitution
  fail authentication for written data.

The caller must discard output on every error. This is not a guarantee against
removal, deletion or rollback by someone controlling the SD card.

## Cache, batching and completion

Requests contain 1–64 sectors, using four-byte-aligned caller buffers. A cache
holds a contiguous window of up to six metadata sectors (3 KiB), enough for any
64-sector batch even when it starts at the end of a metadata group. Cache hits
avoid repeated metadata reads. A cache miss reads the needed consecutive metadata
sectors in one request; it never reads old payloads for a complete overwrite.

Write order:

1. Load containing metadata as needed to preserve neighbouring entries.
2. Calculate new tags over the supplied ciphertext and update cached entries.
3. Complete one batched ciphertext write.
4. Complete one batched write of the affected metadata sectors.
5. Return success only after both complete.

The block-device write contract requires backend completion (the RP2354 SD adapter
waits for card busy completion and checks status). There are no dirty metadata
entries left awaiting a later flush after success. A failed write invalidates the
cache/session. There is no unconditional readback or persistent duplicate payload.

Reads combine adjacent written sectors into block reads and skip unset runs.
They validate every requested written tag before success. Interrupted updates
may produce authentication errors; torn shared metadata may affect neighbours.
This module does not promise sector atomicity or old/new recovery. An SD card's
internal power-loss behaviour is outside the firmware completion guarantee.

Cached metadata assumes exclusive media ownership. After removal, replacement
or external editing, reopen before further use; hot external mutation while
retaining a cache is not an emulated coherent operation.

## Measurements and tests

Stats separate HMAC time, ciphertext I/O time, metadata I/O time, and metadata
sectors read/written. Format metadata time is recorded separately in the benchmark
configuration response. Key setup covers cipher and HMAC prepared contexts.

Desktop tests include an RFC 4231 known-answer HMAC case, comparisons against
OpenSSL including long keys, a six-sector metadata-boundary batch, cache reuse,
neighbour preservation, remounts, mixed unset/written reads, overflow bounds,
corruption, relocation, replay, explicit deletion, and interruption between data
and tag writes. The Python runner also exercises the real C engine with file-backed
storage in authenticated and baseline modes.

See [benchmark instructions](../../firmware/bench/README.md) for `--integrity`.
