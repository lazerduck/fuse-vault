# Protocol 4 hardware: HMAC and metadata comparison

Date: 2026-09-16. Board: `317741A1459A6F94`. CPU: 150 MHz; requested four-bit SD clock: 25 MHz.
UF2 supplied to runner: `075e89d60aaa81450c36dcaf07749ad5d9d946e0ed6139f20e4ef6a162ac1945`.

All 20 configurations verified: five stacks × two batch sizes × two integrity modes. Each wrote/read 1 MiB, using the same card and firmware. A separate 64 KiB authenticated AES smoke test also passed. The baseline matrix ran first, followed by the authenticated matrix; these are single measurements, not interleaved repetitions.

Sources: [baseline JSON](v4-baseline-matrix.json), [authenticated JSON](v4-auth-matrix.json), [smoke test](v4-auth-first-flow.json), [handshake](v4-info.json).

## Paired end-to-end throughput

Payload KiB/s, host request through complete response; no password handling, VMK wrapping or filesystem. Authenticated mode uses software SHA-256, full 32-byte HMAC tags and packed metadata.

| Stack | Batch bytes | Baseline write | Auth write | Baseline read | Auth read |
|---|---:|---:|---:|---:|---:|
| raw | 4096 | 574.4 | 400.4 | 704.0 | 525.3 |
| raw | 32768 | 616.5 | 478.1 | 754.1 | 554.8 |
| aes | 4096 | 406.7 | 309.4 | 477.4 | 381.2 |
| aes | 32768 | 432.2 | 359.3 | 497.3 | 401.7 |
| camellia | 4096 | 372.9 | 291.0 | 425.6 | 348.2 |
| camellia | 32768 | 400.0 | 330.4 | 443.4 | 365.9 |
| aes,camellia | 4096 | 292.7 | 237.4 | 328.0 | 279.0 |
| aes,camellia | 32768 | 313.8 | 268.9 | 340.0 | 292.2 |
| camellia,aes | 4096 | 292.4 | 238.7 | 328.0 | 279.0 |
| camellia,aes | 32768 | 312.9 | 269.0 | 339.9 | 292.2 |

## Authenticated stage timings

Milliseconds per 1 MiB direction at 32 KiB batches. Metadata reads/writes count physical 512-byte sectors, not commands. Ciphertext I/O remains separate.

| Stack | Direction | Crypto ms | HMAC ms | Data SD ms | Metadata ms | Metadata read/write sectors |
|---|---|---:|---:|---:|---:|---:|
| raw | write | 0.0 | 468.8 | 128.4 | 44.3 | 166/166 |
| raw | read | 0.0 | 468.8 | 91.7 | 12.5 | 166/0 |
| aes | write | 704.0 | 469.4 | 131.6 | 42.1 | 166/166 |
| aes | read | 701.5 | 469.6 | 91.7 | 12.7 | 166/0 |
| camellia | write | 951.2 | 469.2 | 131.3 | 44.7 | 166/166 |
| camellia | read | 951.7 | 469.1 | 91.8 | 12.9 | 166/0 |
| aes,camellia | write | 1656.7 | 469.8 | 133.0 | 43.1 | 166/166 |
| aes,camellia | read | 1655.3 | 469.9 | 91.7 | 13.0 | 166/0 |
| camellia,aes | write | 1656.9 | 469.7 | 130.1 | 45.0 | 166/166 |
| camellia,aes | read | 1655.7 | 470.0 | 91.8 | 13.0 | 166/0 |

## Interpretation

- The encrypted/authenticated flow works on the tested board/card: every returned payload byte matched the laptop input. This is a hardware round-trip check; adversarial corruption and interruption behaviour remain covered by desktop tests, not hardware fault injection.
- At 32 KiB batches, AES falls from about 432/497 to 359/402 KiB/s write/read when authentication is enabled. The two-layer stacks fall from about 313–314/340 to 269/292 KiB/s.
- The observed throughput reduction is about 17–19% for AES and 14% for the two-layer stacks at 32 KiB. This is not a statistically established constant or a pure HMAC cost: it includes metadata traffic and normal run variability.
- Metadata formatting is logged in each configuration response and excluded from transfer throughput. Each authenticated 1 MiB case uses 137 metadata sectors (70,144 bytes) plus data, for a total footprint of 1,118,720 bytes.
- HMAC currently uses the portable software SHA-256 implementation. These results do not measure the RP2354 SHA accelerator.
- No firmware optimization or additional security feature was introduced during this comparison. The device-local root, VMK/password lifecycle, attempt counter and revocation are still future modules.
