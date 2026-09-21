# V2 portable FIDO engine validation — 2026-09-20

F1 portable baseline passed. This does not enable FIDO on the board. The formal
remaining stages and constraints are in [the ledger](../docs/v2-fido-progress.md).

## Delivered

- V2-owned pico-fido/Pico Keys/TinyCBOR sources with pinned provenance and licenses.
- Opt-in `FV_ENABLE_FIDO_ENGINE` target, sharing V2's Mbed TLS 3.6 build/ABI.
- Required built-in UV callbacks, no external PIN setup; direct UV and scoped
  protocol 1/2 UV-token dispatch; ES256 and credential self-attestation.
- Zero counters and no counter-only assertion writes, matching accepted replay risk.
- Documented input/output bounds, serialized ownership and close-time wiping.
  Pre-cancelled commands and short response buffers fail before mutation.
- Independent python-fido2 client against the actual C engine and test-only RAM
  snapshot/UV/presence callbacks. No upstream USB/OTP/physical flash code linked.

## Host commands and results

```sh
cmake -S . -B build-fido -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fido -j4
ctest --test-dir build-fido --output-on-failure

cmake -S . -B build-fido-sanitize -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DFV_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fido-sanitize -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-fido-sanitize --output-on-failure

cmake -S . -B build-fido-disabled -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fido-disabled -j4
ctest --test-dir build-fido-disabled --output-on-failure
```

Results: 27/27 enabled; 27/27 ASan/UBSan; 25/25 disabled. LeakSanitizer was disabled
for this traced environment; no leak validation is claimed. Python dependencies
are required at configuration rather than silently skipping the independent test.

The reference test verifies public-key signatures on self-attestation and login,
resident and nonresident keys, UV/UP flags, exclusion/wrong-RP denial, both scoped
token protocols, wrong permissions/RP, token timeout/reopen invalidation, host
PIN rejection, presence and UV denial, durable callback failure, RNG failure,
session faulting, deletion/reset across reopen, reset timeout, pre-cancellation,
malformed CBOR and oversized requests. A 64 KiB store filled after 44 records with
the fixture's 100-character names; it returned KEY_STORE_FULL and the earliest
credential still signed correctly after reopen. This is not a universal passkey
capacity promise: record sizes vary. Each protocol exercises 1,000 seeded malformed
messages; that is a regression smoke test, not exhaustive fuzzing or conformance.

## ARM evidence

```sh
cmake -S . -B build-fido-arm -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DFV_BUILD_TESTS=OFF -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  '-DCMAKE_C_FLAGS=-mcpu=cortex-m33 -mthumb -fstack-usage' -DCMAKE_BUILD_TYPE=Release
cmake --build build-fido-arm --target fv_fido_engine -j4
```

The library compiled and a temporary main calling `fv_fido_engine_command` linked
against it and `fv_crypto` using `--specs=nosys.specs -Wl,--gc-sections -lm`.
Newlib emitted its expected unimplemented-syscall warnings for this standalone
smoke executable. This is not a board image or executable hardware test.

Standalone smoke ELF: text 261,416 bytes; data 2,228; BSS 49,780. The caller's
64 KiB object image is not allocated in that smoke main, nor is the rest of V2
firmware. Do not interpret these figures as total product memory usage.
Largest individual engine stack frames: assertion 6,696 bytes, registration 5,896,
credential management 1,712. Nested call chains, Mbed TLS heap and runtime stack
watermarks still require integrated firmware measurement.

## Remaining gates

F2 encrypted SD persistence; F3 HID/MSC/UI/real unlock; F4 local UI/browser server;
F5 hardware interoperability/fault testing. Review upstream security changes before
hardware/release readiness. Bind reset timing to USB enumeration rather than engine
reopen during F3. Preserve physical-display and production-hardening release gates.
