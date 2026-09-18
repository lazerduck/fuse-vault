# Benchmark engine and protocol

This module connects an already-prepared cipher pipeline to a raw block device.
It has no Pico or USB dependency. `fv_bench_engine` receives a device, opener and
microsecond timer; `fv_bench_execute` transforms an owned buffer and returns
status/timings. Desktop tests and RP2354 firmware execute this same code.

## Protocol 5

All integers are unsigned and little-endian. No C structs are transmitted.
One request at a time; no automatic retry or magic-byte scanning after an invalid
header. Requests and responses are carried over USB CDC binary bulk data.

Request: eight 32-bit words (32 bytes):

| Word | Field |
|---|---|
| 0 | Magic `0x33425646` |
| 1 | Version `5` |
| 2 | Operation: INFO=1, CONFIG=2, WRITE=3, READ=4, END=5, AUTH_CONFIG=6 |
| 3 | Host sequence, echoed in response |
| 4 | Logical sector, or CONFIG/AUTH_CONFIG write token `0x45524153` |
| 5 | Block count, or CONFIG/AUTH_CONFIG scratch capacity (1–8192 sectors) |
| 6 | Request payload bytes; WRITE only, exactly blocks × 512 |
| 7 | CONFIG/AUTH_CONFIG stack: one algorithm ID per byte, first layer in low byte |

Stack IDs: AES-256-XTS=1, Camellia-256-XTS=2. Zero whole stack means raw mode;
otherwise IDs must be contiguous, with no zero holes. Up to four layers. Other
operations require the algorithm field to be zero. INFO/END require zero args.
WRITE/READ accept 1–64 sectors within the configured scratch range starting at
physical LBA 0. Logical LBA supplies the XTS tweak.

Response: 104 bytes:

| Offset | Type | Field |
|---|---|---|
| 0, 4 | u32 | Magic, version |
| 8, 12 | u32 | Operation OR `0x80000000`, echoed sequence |
| 16, 20 | u32 | Status, response payload bytes |
| 24, 32, 40 | u64 | Crypto, SD, key-setup microseconds |
| 48 | u64 | Card capacity in sectors (zero before initialization) |
| 56, 60 | u32 | CPU Hz, requested SD Hz |
| 64, 72 | u64 | HMAC microseconds, metadata I/O microseconds |
| 80, 88 | u64 | Metadata sectors read, written |
| 96, 100 | u32 | HMAC backend (0 software, 1 Pico CPU-fed accelerator), self-check (1 passed) |

Only successful READ replies carry payload (blocks × 512 bytes). Error replies
have no payload. Status: 0=success, 1=invalid request, 2=not ready, 3=SD error,
4=crypto error, 5=sector integrity error. A transport/crypto failure clears the session and returns no data. Integrity
failure returns no data for the batch but leaves the session usable.

The header decoder rejects oversized/inconsistent payload lengths before USB
receives into the 32 KiB buffer. Invalid framing poisons the connection until
reconnect. Semantically invalid, correctly framed requests receive an error.
There is no HMAC on protocol framing: USB provides transport error detection and
the laptop compares final plaintext. This is a test protocol, not authentication.

## Ownership and completion

The USB core owns the shared buffer while receiving and transmitting. Queueing
a request transfers ownership to the worker; the response queue transfers it
back. A complete response is copied to the USB FIFO before the next request can
reuse the buffer. Partial frames never reach the engine. A disconnect discards
partial transport state; an in-flight worker operation completes before its
buffer is reused and a queued reset clears the old session.

CONFIG and AUTH_CONFIG replace any existing session. AUTH_CONFIG also formats
packed metadata and enables sector HMAC. The first ceil(N/15) physical sectors
then hold metadata, followed by N ciphertext sectors. This increases the
physical scratch footprint above the requested logical size.

CONFIG replaces any existing session. Fixed public 64-byte test keys use byte
`i + 71 * layer_index` modulo 256, data-key half first. This is reproducible
benchmark material, not a production derivation. Reconfiguration does not write
metadata or make previous scratch contents meaningful under a new stack.

WRITE encrypts every sector once per layer and issues one batched block-device
write. The block-device contract requires completed writes, including card busy
completion; the native SD adapter checks status before success. READ performs
one batched SD read then reverses the cipher stack. END syncs and clears keys.
There are no background write acknowledgments, old-payload reads, per-sector
nonce generation, persistent vault headers or write-readback verification.
Authenticated mode stores tags and state via the [storage module](../storage/README.md),
verifies ciphertext before decryption, and skips decryption for unset sectors.
It uses a distinct public integrity key (byte i = 0xd3 XOR i) and public volume
ID (byte i = 0x80+i), for benchmarking only.

See [board instructions](../../firmware/bench/README.md) for builds and runs.


Protocol 5 adds backend identity and a startup self-check result. The engine runs
its selected HMAC backend against known/software reference results before the
first request. Failure stops operations before any SD work. The desktop runner
checks version/backend/self-check and logs them; it rejects older firmware.
The HMAC/tag bytes and storage layout are unchanged from protocol 4.
