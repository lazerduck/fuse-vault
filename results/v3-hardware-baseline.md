# First V3 hardware pipeline baseline — 2026-09-16

Board serial: `317741A1459A6F94`. CPU: 150 MHz. Requested native four-bit SD clock: 25 MHz.
UF2 SHA-256 supplied to runner: `ac1422b7366f533b088f035461b3c845fc771bcece243ae55d47f945d3dc2fa7`.

All 25 configurations completed and verified. Each wrote 4 MiB and read it back: 100 MiB written and 100 MiB verified reads overall. Fixed public keys; no HMAC, tags or vault metadata. One run per configuration on the same card/range.

Sources: [full matrix](v3-matrix.json), [initial 64 KiB AES smoke test](v3-first-flow.json), [USB handshake](v3-first-info.json).

## End-to-end throughput

KiB/s of payload, measured from request to complete response. This includes CDC USB, host processing, queue handoff and SD/crypto. It excludes plaintext generation and read comparison; full phase wall timings are also in the JSON.

| Stack | Batch bytes | Write KiB/s | Read KiB/s |
|---|---:|---:|---:|
| raw | 512 | 318.6 | 501.3 |
| raw | 4096 | 564.2 | 706.6 |
| raw | 8192 | 602.7 | 733.6 |
| raw | 16384 | 634.2 | 749.4 |
| raw | 32768 | 650.3 | 755.1 |
| aes | 512 | 252.5 | 335.5 |
| aes | 4096 | 414.8 | 457.3 |
| aes | 8192 | 432.5 | 466.0 |
| aes | 16384 | 435.3 | 470.8 |
| aes | 32768 | 444.5 | 470.8 |
| camellia | 512 | 238.1 | 327.1 |
| camellia | 4096 | 378.3 | 432.3 |
| camellia | 8192 | 391.6 | 441.4 |
| camellia | 16384 | 398.7 | 444.7 |
| camellia | 32768 | 404.9 | 446.0 |
| aes,camellia | 512 | 204.0 | 250.4 |
| aes,camellia | 4096 | 298.1 | 320.2 |
| aes,camellia | 8192 | 309.8 | 324.8 |
| aes,camellia | 16384 | 308.7 | 327.3 |
| aes,camellia | 32768 | 311.6 | 328.7 |
| camellia,aes | 512 | 201.7 | 250.5 |
| camellia,aes | 4096 | 291.3 | 320.2 |
| camellia,aes | 8192 | 302.7 | 325.0 |
| camellia,aes | 16384 | 306.7 | 327.3 |
| camellia,aes | 32768 | 310.0 | 328.6 |

## Stage timings at 32 KiB batches

Seconds per 4 MiB direction. The gap between total and SD/crypto is the combined USB/host/protocol/handoff overhead, not a direct USB-only measurement.

| Stack | Direction | Crypto s | SD s | End-to-end s |
|---|---|---:|---:|---:|
| raw | write | 0.000 | 0.454 | 6.299 |
| raw | read | 0.000 | 0.371 | 5.425 |
| aes | write | 2.814 | 0.457 | 9.214 |
| aes | read | 3.265 | 0.371 | 8.701 |
| camellia | write | 3.745 | 0.450 | 10.116 |
| camellia | read | 3.751 | 0.373 | 9.183 |
| aes,camellia | write | 6.556 | 0.451 | 13.145 |
| aes,camellia | read | 7.030 | 0.373 | 12.461 |
| camellia,aes | write | 6.556 | 0.454 | 13.213 |
| camellia,aes | read | 7.032 | 0.373 | 12.464 |

## Interpretation

- Batching materially improves this flow, with the largest gain between 512-byte and 4 KiB requests.
- Raw 32 KiB transfers spend about 0.45 s writing SD and 0.37 s reading SD, out of 6.30 s and 5.42 s end-to-end respectively. Optimizing SD alone cannot remove most of this measured overhead.
- AES at 32 KiB reaches about 445 KiB/s write and 471 KiB/s read; the two-layer stacks reach about 310–312 KiB/s write and 329 KiB/s read.
- These are sequential, one-outstanding-batch CDC measurements. They do not establish a USB MSC/filesystem limit or performance with future authentication and metadata.
- No firmware optimization was made during this run. Repeat measurements would be needed to assess small differences or variability.
