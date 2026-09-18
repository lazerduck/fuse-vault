# HMAC SHA-256 backends

The public HMAC interface and storage tags are identical across backends.
Desktop defaults to software SHA-256. The RP2354 benchmark defaults to hardware
with `FV_HMAC_PICO=ON`; `OFF` selects the software reference for comparisons.

## Software

Mbed TLS SHA-256 prepares inner and outer HMAC states once at key initialization.
Per-sector calls clone those states, append context/ciphertext, and finish the
inner and outer hashes. This remains compiled into the board image as a self-test
reference, even when the hardware backend is selected.

## Pico accelerator

`pico_hmac.c` uses the SDK `pico_sha256` API, not direct register manipulation:

- Uses `pico_sha256_try_start` with big-endian digest output and DMA disabled.
- Uses CPU-fed blocking updates; no additional DMA channel is claimed, so it
  does not compete with SD DMA allocation.
- Replays the prepared 64-byte ipad/opad for each hash. The SDK does not expose
  restoration of a saved partial hash state. There is no per-sector key derivation.
- Finishes each hash through the SDK to complete padding and release the lock.
- Lock contention returns failure immediately. No infinite wait for lock ownership,
  no touching another owner's state, and no silent software fallback.
- Detected input-not-ready errors abandon the hash through `finish(NULL)`, release
  ownership and clear output. The SDK's hardware-ready waits during hashing remain
  blocking; this does not add a hardware-fault timeout to those SDK operations.
- Inner digests and local state are cleared after use. The HMAC context, including
  prepared pads and reference states, is erased when its owner clears the session.

The benchmark worker on core 1 owns HMAC calls. The SDK lock also protects access
if another component later uses SHA. The lock is released between the inner and
outer hashes; if another caller takes it then, the current HMAC fails safely.

Hashing long HMAC keys down to 32 bytes and preparing reference states still uses
software at initialization, outside per-sector timing. Storage uses 32-byte keys.
The stored sector-tag construction, byte order and metadata layout are unchanged.

## On-board self-check and evidence

Before the first benchmark request, the engine runs:

- RFC 4231 HMAC case 1 against the published answer.
- Selected-backend versus software tags with 32-byte and 131-byte keys.
- Unaligned 40-byte context plus payload lengths 0, 1, 55, 56, 63, 64, 65 and 512.

Failure prevents benchmark/configuration work and returns a crypto error, before
any SD initialization or formatting. Success is retained for this engine lifetime;
it is outside timed data operations. Every response identifies the selected
backend and the self-check result. It is not a fallback mechanism.

Host tests simulate the SDK contract to check lock ownership, failed inner/outer
starts, error cleanup, repeated use and no DMA requests. The same adapter also
passes storage corruption tests and the file-backed laptop-to-engine matrix.
The mock does not emulate silicon. The actual board passed the self-check and
all ten authenticated benchmark configurations on 2026-09-16. Complete HMAC time
fell from about 470 ms to 74 ms per MiB. See the [hardware comparison](../../../results/v5-sha-comparison.md),
including the separate AES encryption timing regression observed in this build.

For this build, the accelerated UF2 is `build-pico/fuse_vault_v2_bench.uf2`.
A protocol-5 software-only comparison build is in `build-pico-software/`.

## Pico SRAM placement

`FV_CIPHERS_RAM` enables source-specific forced declaration headers for upstream
AES and Camellia. The vendor source is not modified. The declarations assign
hot functions and Camellia S-boxes to distinct `.time_critical.*` sections,
which the Pico SDK links into SRAM and copies at startup. AES tables already
live in BSS. This option is ON for firmware, OFF for desktop; `FV_AES_RAM`
remains available for the earlier AES-only diagnostic with `FV_CIPHERS_RAM=OFF`.

The linked combined build uses 3,760 additional initialized RAM bytes compared
with the flash-based build. Its eight desktop suites pass and SRAM symbols/table
bytes have been checked. All 30 combined on-board cases passed across three
matrix runs; see the [results](../../../results/v5-ciphers-ram-benchmark.md). This is a memory
placement change, not a new cipher or storage format. The declaration headers
depend on Mbed TLS internal names: recheck placement and table sizes when
upgrading the dependency.
