# Source layout and scene-based UI refactor — 2026-09-27

Active first-party sources now live under `src/shared`, `src/pico` and `src/host`.
See [source guide](../src/README.md) and [UI guide](../src/shared/ui/README.md).

The UI now uses a static scene registry with entry, key and render callbacks;
menus keep labels and actions together. Credential workflows use a single named
flow state. Shared debounce logic is separate from Pico pin access, and TFT and
accelerated crypto sources live on the Pico side. No device was flashed or used.

## Validation

- Fresh pre-change desktop baseline: 38/38 tests passed with FIDO enabled.
- Refactored desktop build: 39/39 tests passed with FIDO enabled.
- ASan/UBSan: 39/39 passed (`ASAN_OPTIONS=detect_leaks=0`; no leak-check claim).
- FIDO-disabled desktop build: 28/28 passed.
- Final focused UI/component/FIDO-adapter reruns after readability edits: 3/3
  passed in both normal and sanitizer builds.
- Device ELF compiled/linked with UI + MSC + FIDO, and with UI + MSC without FIDO.
- Benchmark and minimal alive firmware ELF targets compiled/linked.
- All 30 offline preview fixtures (PBM pixels and generated gallery assets)
  matched the pre-refactor output exactly.
- Generated TinyUSB MSC driver tests passed with 4/16/32 KiB buffers; USB task
  budget test reproduced the original starvation and passed with the patched task.
- Active Markdown links and source references checked; `git diff --check` passed.

The Pico builds used `/home/adam/pico-sdk` and `-DPICO_NO_PICOTOOL=1`, matching the
existing local firmware build's compile/link configuration. UF2 conversion and
physical button/display/USB acceptance were not performed.

## Reproduction

From the repository root, with `PICO_SDK_PATH` pointing to the SDK:

```sh
cmake -S . -B build-check \
  -DFV_MBEDTLS_DIR="$PICO_SDK_PATH/lib/mbedtls" -DFV_ENABLE_FIDO_ENGINE=ON
cmake --build build-check -j4
ctest --test-dir build-check --output-on-failure

cmake -S src/pico/device -B build-device-check \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_NO_PICOTOOL=1 \
  -DFV_DEVICE_UI=ON -DFV_USB_MSC=ON -DFV_USB_FIDO=ON -DFV_DEBUG_SCREEN=ON
cmake --build build-device-check -j4

python3 src/host/tools/ui_preview.py --output /tmp/fuse-vault-ui-preview
python3 src/host/tools/test_usb_pipeline_driver.py \
  --sdk "$PICO_SDK_PATH" --firmware-build build-device-check
python3 src/host/tools/test_usb_task_budget.py \
  --sdk "$PICO_SDK_PATH" --firmware-build build-device-check
```

Use new build directories for relocated Pico source entry points. Existing host
executables retain their names and remain at the build-directory root. Historical
reports and the V1 reference archive retain their historical source paths.
