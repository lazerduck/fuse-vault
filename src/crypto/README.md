# Standalone crypto building blocks

This is the first V2 component: a C11 library for independently encrypted
512-byte sectors, with AES-256-XTS and Camellia-256-XTS and ordered pipelines.
It does not depend on USB, SD, a filesystem, Pico SDK runtime, or V1 code.

## Layout and C interface

- `include/fuse_vault/crypto.h`: public types and functions.
- `cipher.c`: validation, lifecycle and shared fixed-sector XTS processing.
- `internal.h`: function-pointer interface (the C equivalent of a small interface).
- `backends/mbedtls.c`: AES and Camellia key setup and block-cipher adapters.
- `pipeline.c`: forward encryption and reverse decryption through 1–4 layers.

Each initialized `fv_cipher` contains prepared key schedules and a pointer to
its implementation's operations. There is no heap allocation. Contexts can be
stack or static objects, and must be zero-initialized before first use. The
current public context storage uses Mbed TLS types to provide correct sizing
and alignment; replacing the backend may change this source-level ABI.

Keys are supplied already derived: 64 bytes per layer, comprising a 32-byte
data key and a 32-byte tweak key. Equal halves and duplicate complete pipeline
keys are rejected. These checks do not prove cryptographic independence: the
future key derivation component must supply independently derived keys.

Input and output are binary buffers of exactly 512 bytes. Same-buffer operation
is supported; partially overlapping buffers are rejected. Buffers and key input
must not overlap the context being initialized or used. A failed operation's
output must not be consumed. No context may be used concurrently by two cores;
its owner must finish operations before clearing or replacing it.

```c
#include "fuse_vault/crypto.h"

fv_pipeline pipeline = {0};
fv_algorithm algorithms[] = {FV_AES_256_XTS, FV_CAMELLIA_256_XTS};
/* layer_keys: two independently derived 64-byte keys supplied by caller. */
if (fv_pipeline_init(&pipeline, algorithms, layer_keys, 2) != FV_OK) {
    /* Handle initialization failure. */
}
/* After successful initialization, transform one sector in place. */
if (fv_pipeline_encrypt(&pipeline, logical_sector_number, sector, sector) != FV_OK) {
    /* Discard output and report failure. */
}
fv_pipeline_clear(&pipeline);
```

Decryption uses `fv_pipeline_decrypt` and reverses layer order automatically.
Clear operations erase prepared schedules; callers remain responsible for
erasing their original key inputs and plaintext buffers.

## XTS convention

The 64-bit logical sector number is serialized little-endian into bytes 0–7 of
the 16-byte data-unit input; bytes 8–15 are zero. Each layer encrypts that input
with its own tweak key, and uses the XTS little-endian GF(2^128) progression.
A sector contains exactly 32 full 16-byte blocks, so ciphertext stealing is not
needed. There is no variable-length or text encryption interface.

AES and Camellia primitives come from Mbed TLS; our shared XTS mode adapter is
project code. Camellia-XTS is experimental here, not a claim of NIST approval.
Changing the LBA convention later would change ciphertext interpretation; this
library has not yet established a complete disk format.

XTS provides confidentiality only. Wrong keys, wrong LBAs and corrupt ciphertext
can decrypt successfully to garbage. Authentication tests belong to the future
HMAC/storage layer; there is intentionally no false authentication-failure
promise in this interface. Do not expose this alone as an authenticated volume.

## Dependency and builds

Use a local Mbed TLS **3.6** source checkout. No automatic download occurs.
Validated with 3.6.6 at commit `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`,
from the Pico SDK's `lib/mbedtls`. Upstream code remains in that checkout under
its own license (Apache-2.0 OR GPL-2.0-or-later); it is not copied into V2.
AES, Camellia, SHA-256, platform zeroization, XTS and library self-tests are enabled.
`hmac.c` adds prepared HMAC-SHA-256 using the SHA-256 primitive.
This configuration is for the crypto target; do not combine it with another
incompatibly configured Mbed TLS library in the same executable.

From the repository root on this machine:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/crypto_bench
```

Replace the dependency path on other machines. Desktop tests need OpenSSL
headers/library; the crypto library and benchmark do not link OpenSSL.
`FV_BUILD_TESTS=OFF` builds only the library. A parent CMake project can instead
set `FV_MBEDTLS_DIR`, add this directory, and link `fv_crypto`.

### Validation

Tests run Mbed TLS known-answer self-tests, compare full AES-XTS sectors against
OpenSSL, and compare Camellia-XTS against an independent test composition using
OpenSSL Camellia and separate polynomial multiplication. They cover high/maximum
LBAs, sector independence, in-place/disjoint buffers, invalid inputs, key clearing,
and all AES/Camellia orderings through four layers. Round trips alone are not the
correctness criterion. Tests remain active in Release builds.

For desktop address and undefined-behaviour sanitizers, configure another build
with `-DFV_SANITIZERS=ON`. In this traced execution environment LeakSanitizer
cannot run; `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize
--output-on-failure` retains address/undefined checks without leak detection.

Cortex-M33 compilation check (builds an archive, not a board application):

```sh
cmake -S . -B build-arm -DFV_BUILD_TESTS=OFF \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  '-DCMAKE_C_FLAGS=-mcpu=cortex-m33 -mthumb' -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j4
```

The desktop benchmark reports setup separately from encryption/decryption of
16 MiB per configuration. It uses public test keys and a single sector buffer;
results measure desktop RAM processing, not correctness, storage batching or
RP2354 throughput. A board runner must supply its own timing/reporting adapter.

## Current boundary

This module now includes HMAC-SHA-256, but not password KDF, VMK wrapping, OTP,
SD or USB.
The separate [board benchmark](../../firmware/bench/README.md) now calls it from
a worker core in the USB → encryption → SD benchmark path. The first
[hardware matrix](../../results/v3-hardware-baseline.md) passed on 2026-09-16. The [Pico HMAC backend](backends/README.md) now uses the SHA accelerator in
board builds; software remains the desktop implementation and on-board reference.
Neither backend is a claim of constant-time execution or
physical side-channel resistance; evaluate the target implementation before
using it with real secrets.
