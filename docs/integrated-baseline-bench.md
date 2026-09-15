# Baseline commands in the established headless firmware

Prepared 2026-09-14. Build and host checks passed. The fixed-USB-C variant now
enumerates and answers debug commands; bulk benchmark results remain pending.
See `usb-route-diagnostic.md` for the corrected hardware observations.

This build uses the ordinary `firmware/CMakeLists.txt`, product `main.c`, board
configuration, native SD backend, USB initialization, descriptors and screen
viewer protocol. It adds an explicitly entered baseline mode. It does not use
the standalone bench entry point, ROM fallback, HardFault handler, aligned
startup trace, alternate USB PID or alternate product descriptor.

Unlike the standalone INFO-only image, normal startup initializes SD and reads
existing vault metadata. An incompatible card can put the application in its
ordinary fault/setup state; headless CDC remains the diagnostic channel. No
formatting is required. Baseline mode may be entered only without an unlocked
volume key or ready encrypted backend. Normal lifecycle/input work is suspended
after entry; power-cycle to resume normal operation.

## First board check

Flash `firmware/build-integrated-bench/fuse_vault.uf2`, not the old
`firmware/build-bench/fuse_vault_bench.uf2`. The board should enumerate as the
existing `cafe:4013` screen-debug device. The existing viewer should work before
bench entry. Close it before running the capture tool.

```sh
python3 firmware/tools/run_baseline_bench.py --integrated --info-only \
  --label 'Board 1 integrated USB check' \
  --uf2 firmware/build-integrated-bench/fuse_vault.uf2 \
  --output firmware/bench-results/board1-integrated-info.json
```

The tool sends `B` and requires a positive baseline-mode handshake before sending
any test commands. Old headless firmware ignores `B`, so it times out without
running tests. INFO-only does not issue additional SD test commands; it does
not undo the normal firmware's startup card checks. Repeating the handshake in
an already active baseline session is supported.

## Raw SD and crypto

After the first USB check passes, use the same disposable card:

```sh
python3 firmware/tools/run_baseline_bench.py --integrated \
  --label 'Board 1 disposable card integrated baseline' \
  --uf2 firmware/build-integrated-bench/fuse_vault.uf2 --erase-sd \
  --output firmware/bench-results/board1-integrated-baseline.json
```

`--erase-sd` destroys the first 4 MiB including partition/vault headers. Omit it
for crypto and raw reads only. Test timings and sizes retain the definitions in
the earlier baseline document. Synthetic crypto does not use device secrets.
No destructive test command is retried automatically.

For the USB-only stage, add `--ram-usb` to the baseline capture command. Identify
the resulting 128 KiB whole disk with `lsblk`, then run
`sudo python3 firmware/tools/bench_ram_usb.py --integrated --device /dev/sdX --output firmware/bench-results/board1-integrated-usb.json`
with that identified device. The utility requires the 4013 USB identity and exact
128 KiB capacity, rejects partitions, opens exclusively, and uses direct I/O.
It will not accept the full-sized SD-backed vault. Do not run SD/crypto tests
concurrently with this measurement. Power-cycle before another full baseline
after attaching the RAM disk.

The standard image's benchmark option defaults OFF and is rejected without
headless development mode or in a release build. Tests verify normal framebuffer
and diagnostic packets still work, and `B` cannot interrupt an in-flight packet.
The baseline build currently has about 233 KiB BSS and reserves the ordinary
64 KiB main stack. This fits the linker limits; it is not a runtime watermark.

## Rebuild

```sh
cmake -S firmware -B firmware/build-integrated-bench \
  -DPICO_SDK_PATH=/home/adam/pico-sdk \
  -Dpicotool_DIR=/home/adam/projects/fuse-vault/firmware/build/_deps/picotool \
  -DFUSE_VAULT_HEADLESS_DEBUG=ON -DFUSE_VAULT_SD_NATIVE=ON \
  -DFUSE_VAULT_BASELINE_BENCH=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build firmware/build-integrated-bench -j4
```
