# Source layout

All active first-party software lives under `src/`. Directory names describe
where the code runs, rather than whether it happens to be called firmware.

```text
src/
  shared/                 Libraries used by both device and desktop builds
    ui/                   Scenes, menus, drawing, credential input, debounce
    crypto/               Cipher interfaces and portable implementations
    storage/              Authenticated block storage and volume format
    security/             Vault lifecycle, keys, policy and persistence logic
    fido/                 FIDO transport, verification and encrypted store
    benchmark/            Shared benchmark engine and protocol
  pico/                   RP2354/Pico SDK code
    device/               Main application, USB, worker queues, UI/FIDO adapters
    platform/             GPIO buttons, TFT, SD, entropy, authority, board headers
    crypto/               SHA accelerator and SRAM placement headers
    benchmark/            Benchmark firmware application
    alive/                Minimal board bring-up application
  host/                   Desktop entry points and hardware simulations
    tools/                UI preview, command-line clients, browser test server
    tests/                Unit/integration tests, SDK stubs and test fixtures
```

`third_party/` contains pinned dependencies and their integration code. `v1/`
is the reference archive, not part of the active build. `build*/` contains
local generated output. Historical reports in `results/` retain the paths used
when they were written.

## Build entry points

Run these commands from the repository root. Use a **new build directory** when
switching from the old `firmware/` source paths: CMake caches its source directory.
The examples assume the Pico SDK is available at `$PICO_SDK_PATH`.

Desktop libraries, tools and tests:

```sh
cmake -S . -B build-host -DFV_MBEDTLS_DIR="$PICO_SDK_PATH/lib/mbedtls"
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

Add `-DFV_ENABLE_FIDO_ENGINE=ON` for FIDO integration tests (requires Python
`fido2` and `cryptography`), or `-DFV_SANITIZERS=ON` for ASan/UBSan. The root
CMake project delegates to `host/CMakeLists.txt`; host test definitions live
in `host/tests.cmake` and `host/fido_tests.cmake`. Executables remain at the
build-directory root.

Main device application, with physical UI, USB storage and FIDO:

```sh
cmake -S src/pico/device -B build-device \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" \
  -DFV_DEVICE_UI=ON -DFV_USB_MSC=ON -DFV_USB_FIDO=ON
cmake --build build-device -j4
```

The application target is still named `fuse_vault_security` to preserve existing
artifact names. Benchmark and bring-up images use `-S src/pico/benchmark` and
`-S src/pico/alive` respectively. Each application links the shared libraries it
needs; it does not contain another implementation of them.

For offline UI screenshots:

```sh
python3 src/host/tools/ui_preview.py --output /tmp/fuse-vault-ui-preview
```

This needs a host C compiler and Pillow; it renders synthetic data without
accessing a device. See [UI architecture](shared/ui/README.md) for the event flow
and how to add a scene or menu action.

## Dependency direction

Pico applications and host tools depend on shared libraries. Shared C code must
not depend on GPIO, USB scheduling, SDK time functions, host filesystems, or
worker queues. Build configuration may select an accelerated Pico backend.

The UI submits `fv_ui_job` data and receives `fv_ui_result` data. The device
adapter owns queue transport and the display driver; scene code owns presentation
and navigation. Expensive storage/crypto operations continue to run on the worker
core, outside input handling and drawing.
