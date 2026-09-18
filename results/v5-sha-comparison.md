# Hardware SHA-256 comparison

Date: 2026-09-16. Board: `317741A1459A6F94`. CPU: 150 MHz; requested four-bit SD clock: 25 MHz.
UF2 supplied to runner: `80e5de7749428540eaa18efc56a9d5e7befcc19a786efdc5a6d7a16a994afaef`.

The board reported `pico-sha256-cpu-fed` and passed its mandatory HMAC known-answer/software-reference self-check. All ten authenticated configurations passed: five stacks × two batch sizes, each writing and reading/verifying 1 MiB (10 MiB each direction).

Sources: [hardware matrix](v5-sha-auth-matrix.json), [software matrix](v4-auth-matrix.json), [hardware handshake](v5-sha-info.json).

## End-to-end throughput

Payload KiB/s, host request through complete response. Both runs use full HMAC tags and the same metadata layout. Raw means no cipher; authentication remains enabled.

| Stack | Batch bytes | Software write | Hardware write | Software read | Hardware read |
|---|---:|---:|---:|---:|---:|
| raw | 4096 | 400.4 | 483.7 | 525.3 | 652.4 |
| raw | 32768 | 478.1 | 578.7 | 554.8 | 706.3 |
| aes | 4096 | 309.4 | 313.2 | 381.2 | 448.8 |
| aes | 32768 | 359.3 | 367.9 | 401.7 | 472.6 |
| camellia | 4096 | 291.0 | 317.2 | 348.2 | 408.6 |
| camellia | 32768 | 330.4 | 375.7 | 365.9 | 430.4 |
| aes,camellia | 4096 | 237.4 | 244.3 | 279.0 | 316.4 |
| aes,camellia | 32768 | 268.9 | 271.5 | 292.2 | 330.9 |
| camellia,aes | 4096 | 238.7 | 247.4 | 279.0 | 316.5 |
| camellia,aes | 32768 | 269.0 | 270.6 | 292.2 | 330.9 |

## Stage timings at 32 KiB batches

Milliseconds per 1 MiB in each direction. These measure complete HMAC operations, including CPU feeding, rather than isolated SHA accelerator throughput.

| Stack | Direction | Software HMAC ms | Hardware HMAC ms | Software cipher ms | Hardware-build cipher ms |
|---|---|---:|---:|---:|---:|
| raw | write | 468.8 | 73.3 | 0.0 | 0.0 |
| raw | read | 468.8 | 73.4 | 0.0 | 0.0 |
| aes | write | 469.4 | 73.4 | 704.0 | 1054.3 |
| aes | read | 469.6 | 73.5 | 701.5 | 714.3 |
| camellia | write | 469.2 | 74.1 | 951.2 | 925.7 |
| camellia | read | 469.1 | 74.3 | 951.7 | 927.0 |
| aes,camellia | write | 469.8 | 74.2 | 1656.7 | 1979.9 |
| aes,camellia | read | 469.9 | 74.3 | 1655.3 | 1640.0 |
| camellia,aes | write | 469.7 | 74.2 | 1656.9 | 1976.7 |
| camellia,aes | read | 470.0 | 74.3 | 1655.7 | 1640.0 |

## Interpretation and limits

- HMAC fell from about 469–470 ms to 73–74 ms per MiB: approximately 6.3× faster, or 84% less HMAC time.
- Encrypted reads improved by about 13–18%. AES writes improved only about 2%; two-layer writes about 1%. Camellia-only writes improved about 14%.
- AES encryption itself increased from approximately 704 ms to 1,054 ms per MiB in this build, offsetting much of the HMAC saving for writes. AES decryption remained close (702 versus 714 ms). This is an unresolved performance regression, not evidence that HMAC acceleration is ineffective.
- A [same-build AES run without integrity](v5-sha-aes-baseline-check.json) also measured 1,054 ms encryption time. An [authenticated repeat](v5-sha-aes-auth-repeat.json) again verified successfully and reproduced the slower encryption. This establishes that per-sector HMAC execution is not required to reproduce the slowdown; the underlying cause is not established.
- The historical software run used protocol 4; this build uses protocol 5, adding eight response bytes for backend/self-test identification. These are sequential measurements across firmware builds, not randomized repetitions or a controlled same-build backend comparison.
- Cipher algorithms, tag construction, metadata layout, data sizes and requested clocks are unchanged. No security-format change or firmware tuning was made during these measurements.
- This verifies hardware HMAC output and successful storage round trips. Hardware fault injection, real key provisioning and unlock remain outside this test.
