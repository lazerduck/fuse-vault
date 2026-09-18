# Both ciphers in SRAM: repeated hardware benchmarks

Date: 2026-09-16. Board: `317741A1459A6F94`. CPU 150 MHz, requested four-bit SD clock 25 MHz.
UF2 supplied to runner: `b351c00f1329c894b509a7ecfef8bc322c3ca2f795e603004bddb4239590e1f3`.

All **30 cases verified**: three passes × five stacks × two batch sizes. Each case wrote 1 MiB and read/verified 1 MiB: **30 MiB written and 30 MiB read** overall. All three handshakes reported hardware SHA and a passing HMAC self-check.

Sources: [pass 1](v5-ciphers-ram-matrix-1.json), [pass 2](v5-ciphers-ram-matrix-2.json), [pass 3](v5-ciphers-ram-matrix-3.json).

## End-to-end payload throughput

KiB/s, median (minimum–maximum) across three passes. Host request through complete response; excludes configuration and test-data generation. Authentication is enabled in every case, including raw (no cipher).

| Stack | Batch bytes | Write KiB/s | Read KiB/s |
|---|---:|---:|---:|
| raw | 4096 | 477.7 (477.5–480.2) | 652.3 (652.2–653.0) |
| raw | 32768 | 575.5 (571.1–576.3) | 706.4 (706.3–706.4) |
| aes | 4096 | 363.9 (359.8–366.5) | 470.4 (469.8–470.5) |
| aes | 32768 | 427.5 (420.7–430.4) | 490.9 (490.8–491.0) |
| camellia | 4096 | 335.2 (334.5–338.1) | 415.5 (415.4–415.7) |
| camellia | 32768 | 385.4 (376.3–387.2) | 432.0 (431.9–432.1) |
| aes,camellia | 4096 | 277.3 (272.0–279.1) | 330.7 (330.5–330.8) |
| aes,camellia | 32768 | 312.0 (305.6–312.1) | 340.6 (340.6–340.6) |
| camellia,aes | 4096 | 277.4 (275.6–278.8) | 330.5 (330.4–330.6) |
| camellia,aes | 32768 | 309.6 (308.1–312.1) | 340.5 (340.5–340.6) |

## Stage timings at 32 KiB batches

Milliseconds per MiB, median (minimum–maximum) across the three passes.

| Stack | Direction | Cipher ms | HMAC ms | Data SD ms | Metadata ms |
|---|---|---:|---:|---:|---:|
| raw | write | 0.0 (0.0–0.0) | 73.2 (73.2–73.2) | 128.1 (125.0–130.5) | 50.2 (44.8–54.3) |
| raw | read | 0.0 (0.0–0.0) | 73.4 (73.4–73.4) | 91.8 (91.8–91.8) | 12.3 (12.3–12.3) |
| aes | write | 634.0 (634.0–634.0) | 73.3 (73.3–73.3) | 133.8 (127.6–140.0) | 44.9 (44.3–57.5) |
| aes | read | 635.9 (635.9–635.9) | 73.4 (73.4–73.4) | 91.8 (91.7–91.8) | 12.3 (12.3–12.3) |
| camellia | write | 918.0 (918.0–918.1) | 73.3 (73.3–73.3) | 126.4 (126.3–133.4) | 44.9 (44.8–45.0) |
| camellia | read | 921.1 (921.1–921.1) | 73.4 (73.4–73.4) | 91.8 (91.8–91.9) | 12.3 (12.3–12.3) |
| aes,camellia | write | 1553.0 (1553.0–1553.0) | 73.3 (73.3–73.3) | 130.8 (125.2–132.9) | 44.9 (42.9–44.9) |
| aes,camellia | read | 1554.0 (1554.0–1554.0) | 73.4 (73.4–73.4) | 91.8 (91.8–91.9) | 12.3 (12.3–12.3) |
| camellia,aes | write | 1555.2 (1555.2–1555.2) | 73.3 (73.3–73.3) | 134.0 (129.2–136.8) | 43.4 (42.5–44.8) |
| camellia,aes | read | 1555.9 (1555.9–1555.9) | 73.4 (73.4–73.4) | 91.8 (91.8–91.8) | 12.3 (12.3–12.3) |

## Interpretation and scope

- AES encryption stays near 634 ms/MiB, compared with 1,054 ms in the hardware-SHA flash build. Camellia returns to about 918 ms/MiB, compared with 1,510 ms in the AES-only RAM diagnostic. The large cipher slowdowns observed in those builds are absent in this combined image.
- Hardware HMAC remains about 73–74 ms/MiB. The cipher ordering produces similar measured throughput, with write variability visible between passes.
- These repeats establish performance for this firmware, board, card and workload. They do not prove immunity to future layout changes or identify the precise cache-conflict mechanism. RAM placement is strongly supported as a practical improvement by the previous experiments.
- The full benchmark flow is exercised: USB input → cipher stack → HMAC/metadata → SD, then authenticated read → decrypt → USB output and byte-for-byte host verification. Public benchmark keys are used; VMK/password/unlock and production volume headers are not part of these measurements.
- The three passes use the same deterministic payload seed and fixed case order. They are repeatability measurements, not randomized workload or endurance testing.
- No hardware corruption or power-loss injection was performed. Those remain separate reliability tests; existing desktop tests cover simulated corruption/interruption.
- Firmware uses 3,760 additional initialized RAM bytes for the cipher/table placement. Normal startup and USB remain flash-based.
