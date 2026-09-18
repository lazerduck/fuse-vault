# USB presence-monitor isolation test

2026-09-14. Build and host tests passed. Application enumeration and a working
screen viewer subsequently observed; presence-counter capture still pending.

## Corrected hardware observation

The initial negative check was not a final firmware result. The host kernel
recorded cafe:4013, serial 317741A1459A6F94, on `/dev/ttyACM0` at 21:08:36.
The user supplied a working viewer screenshot showing mode selection, uptime
34 seconds, successful boot recovery and 14 SD read requests. An elevated host
check confirmed the board and running `usb_screen.py` process. A simultaneous
ordinary sandbox check could not initialize libusb or see `/dev/ttyACM0`.

Earlier elevated checks also preceded this enumeration; sandbox restrictions
alone do not explain the whole timeline. Do not infer a firmware startup fault
from those snapshots, or attribute the delay to SD, without further evidence.
No additional image is needed to collect the current diagnostic counters.

Subsequent live communication succeeded. The `B` command returned
`Lock the vault before entering bench mode`; no benchmark commands were sent.
A normal timing capture then succeeded and is saved in
`firmware/bench-results/board1-before-baseline.json`. It records one
authentication taking 12.270161 s: PBKDF2 3.156044 s, KMAC 9.084505 s, followed
by storage activation taking 0.030935 s. The maximum USB service gap was
12.434717 s. These observations establish working bidirectional CDC and a
long synchronous authentication stall. They do not measure bulk USB or raw SD
throughput. Reconnect without unlocking before entering baseline mode.

The unchanged diagnostics image enumerated as cafe:4013 at 20:54:12 and
disconnected at 20:54:20. The user reports no physical intervention. Earlier
working sessions used the same board serial 317741A1459A6F94. This does not
establish the cause, but it provides a post-enumeration failure to investigate.

The normal connector guard latches off on any mismatched duplicate pair,
simultaneous connector presence, or departure from the selected connector.
An isolated unexpected sample is sufficient. Headless logical vault detach
normally retains serial USB, whereas a connector trip disables the physical mux.

## Controlled diagnostic

Image: `firmware/build-usb-route-diagnostic/fuse_vault.uf2`.

Use USB-C only. Leave USB-A empty. This explicitly diagnostic image supplies a
fixed USB-C selection to the existing connector guard while recording the actual
GPIO inputs. It does not change the normal build's connector policy. CMake and
source checks prohibit the override outside the headless baseline development
profile. It is not suitable for general dual-connector use.

Normal startup, headless descriptors, SD initialization and runtime are retained.
No SD benchmark or destructive write runs automatically. The standalone startup
and ROM-fallback diagnostics are not used.

After flashing and leaving the board connected, capture:

```sh
python3 firmware/tools/run_baseline_bench.py --integrated --info-only \
  --label 'Board 1 fixed USB-C initial' \
  --uf2 firmware/build-usb-route-diagnostic/fuse_vault.uf2 \
  --output firmware/bench-results/board1-usb-route-initial.json
```

Repeat INFO after at least 30 seconds to a different file. The samples accumulate
since startup, including before bench entry. Counts saturate at UINT32_MAX.
`presence_changes` counts observed pattern changes, not physical edge counts.

Each four-bit `presence_pattern` is GPIO2 / GPIO3 / GPIO16 / GPIO17 from low to
high bit, sampled in the original pair order (2,16,3,17). Patterns are not
simultaneous electrical measurements. Histogram entries contain sample counts:

- 10: both USB-C sense pins high, USB-A pins low (expected in this setup).
- 0: no connector sensed.
- 5: USB-A only.
- 15: both connectors sensed.
- Other values: at least one duplicate pair disagrees.

If fixed routing stays up and non-10 patterns occur, the normal monitor has
observed conditions sufficient to disconnect. Investigate sensing, sampling and
appropriate filtering; do not automatically label the board defective or ship
the diagnostic override. If only pattern 10 occurs, stability alone is weaker
evidence because timing changes can affect intermittent behavior. If the fixed
route also disappears, this test has not established a connector-monitor cause.

Host tests cover all 16 patterns in both normal and diagnostic builds, including
the normal latched failure, diagnostic histogram capture, and disable-before-route
ordering. They do not validate electrical levels on the board.

Rebuild with the integrated baseline CMake options plus
`-DFUSE_VAULT_BENCH_FIXED_USB_C=ON`, in the separate output directory above.
