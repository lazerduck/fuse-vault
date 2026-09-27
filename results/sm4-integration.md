# SM4 integration validation

Validated 2026-09-26 on the current working tree, including the existing FIDO/UI
work. No device was flashed and no SD contents were changed.

- Release desktop build with FV_ENABLE_FIDO_ENGINE=ON: all 37 CTest suites pass.
- Debug build with address/undefined sanitizers: all 37 suites pass with
  ASAN_OPTIONS=detect_leaks=0 and UBSAN_OPTIONS=halt_on_error=1. LeakSanitizer is
  disabled for this environment. The final additional effective-key suffix
  checks also pass a targeted sanitizer rerun of the crypto suite.
- Published single-block and million-iteration SM4 answers pass. Full sectors
  match an independent OpenSSL SM4 + XTS reference at zero, ordinary, high and
  maximum LBAs. All 120 one-to-four-layer combinations match independent output.
- Independent Python/OpenSSL fixtures verify SM4-only and AES/SM4/Camellia/SM4
  VMK envelopes. Existing AES/Camellia fixture bytes are unchanged. A valid outer
  HMAC with a corrupt SM4 wrap fails without publishing any VMK bytes.
- Mixed-stack vault lifecycle tests cover creation, unlocking, I/O, credential
  changes and failures. FIDO snapshot lifecycle tests cover SM4-only and mixed
  stacks, including reopen and credential changes.
- Pico security firmware builds with UI, MSC and FIDO enabled. ELF and binary:
  build-pico-sm4/fuse_vault_security.elf and fuse_vault_security.bin.
  PICO_NO_PICOTOOL=1 follows the existing offline build configuration; no UF2
  was produced. Hardware execution and Pico throughput remain unmeasured.
- Linker symbols confirm sm4_encrypt at 0x2000174c (1790 bytes) and SM4_T at
  0x20002cf0 (1024 bytes), both in SRAM. These are symbol sizes, not a complete
  before/after RAM delta. The existing context union accommodates SM4 schedules.

Build commands:

```sh
cmake -S . -B build-sm4 -DCMAKE_BUILD_TYPE=Release \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls -DFV_ENABLE_FIDO_ENGINE=ON
cmake --build build-sm4 -j4
ctest --test-dir build-sm4 --output-on-failure -j4
cmake -S . -B build-sm4-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DFV_SANITIZERS=ON
cmake --build build-sm4-sanitize -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sm4-sanitize --output-on-failure -j4
cmake -S firmware/security -B build-pico-sm4 \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DFV_DEVICE_UI=ON -DFV_USB_MSC=ON -DFV_USB_FIDO=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico-sm4 -j4
```

One desktop RAM benchmark run (MiB/s; public test keys, no I/O/authentication):

| Cipher | Encrypt | Decrypt |
| --- | ---: | ---: |
| AES-256-XTS | 187.32 | 187.69 |
| Camellia-256-XTS | 98.82 | 132.17 |
| SM4-128-XTS | 160.61 | 161.84 |
| AES → SM4 | 95.61 | 95.02 |

These host measurements do not predict Pico throughput.
