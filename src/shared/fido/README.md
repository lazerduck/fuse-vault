# Portable FIDO engine and SD journal

Firmware uses the bounded storage callbacks in `fido_engine.h` with
`fv_fido_journal` from `fido_journal.h`. The 824 KiB filesystem stays on SD; no
complete plaintext image is allocated. The 1 MiB private region and USB geometry
are unchanged. See [format, migration and recovery](../../../docs/AI/v2-fido-storage.md)
and [user-visible behaviour](../../../docs/FIDO2.md).

The engine supports 512 resident slots, built-in UV, physical approval, zero
signature counters, host credential management and local management. SD callbacks
are serialized by the firmware worker. Close the engine and journal before lock,
credential changes or media replacement. A storage failure faults the session.
Record views have function-scoped ownership; verification tokens have dedicated
session buffers. No API returns a borrowed pointer into SD storage.

The old RAM-image/snapshot API is retained for compatibility tests and migration
fixture generation, not firmware operation. Never use it to write a converted
card. Import provenance remains in
[the vendor notes](../../../third_party/pico_fido/README.fuse-vault.md).

## Build and test

```sh
cmake -S . -B build-fido \
  -DFV_MBEDTLS_DIR=/home/adam/pico-sdk/lib/mbedtls \
  -DFV_ENABLE_FIDO_ENGINE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fido -j4
ctest --test-dir build-fido --output-on-failure
```

Python `fido2` and `cryptography` are required. Use `-DFV_SANITIZERS=ON` for
ASan/UBSan; in this traced environment set `ASAN_OPTIONS=detect_leaks=0` (no leak
check claim). Journal interruption tests take several minutes. Hardware acceptance
still requires actual power cuts, timing and peak RAM under combined USB/FIDO use.
