# Storage timing test

Use `firmware/build-native-sd/fuse_vault-diagnostics.uf2`. This keeps native
four-bit SD at 25 MHz, encryption and all previous optimisations. No reformat
or provisioning is needed. Safely eject before flashing, then fully remove USB
power and reconnect. Keep the damaged physical display unplugged.

Restart the viewer from the current source. Close it before each capture: the
capture tool and viewer share the serial interface. Closing the viewer does
not disconnect the mass-storage drive. Do not run other serial monitors.

## First run: locate the unlock and initial-read delay

1. Unlock normally with the viewer. Note the approximate time from final OK to
   the unlocked screen, and separately to the drive becoming available.
2. Before copying, deleting or browsing files, close the viewer and capture:

```sh
python3 /home/adam/projects/fuse-vault/firmware/tools/capture_diagnostics.py --label unlock --output /home/adam/projects/fuse-vault/firmware/bench-results/unlock.json
```

3. Leave the drive idle for about 30 seconds, then repeat:

```sh
python3 /home/adam/projects/fuse-vault/firmware/tools/capture_diagnostics.py --label idle --output /home/adam/projects/fuse-vault/firmware/bench-results/idle.json
```

Do not use `--reset` before these two captures: we need the unlock history.
The firmware knows when storage is exposed, but cannot know when the desktop
considers the filesystem ready. That part needs your observed time.

## File-operation comparison

Once the drive is ready, clear just the new diagnostic counters:

```sh
python3 /home/adam/projects/fuse-vault/firmware/tools/capture_diagnostics.py --reset --label file-start --output /home/adam/projects/fuse-vault/firmware/bench-results/file-start.json
```

Copy one fixed test file (roughly 1–2 MiB), wait for the operation to finish,
then safely eject so host-buffered writes have completed. Capture:

```sh
python3 /home/adam/projects/fuse-vault/firmware/tools/capture_diagnostics.py --label copy-viewer-off --output /home/adam/projects/fuse-vault/firmware/bench-results/copy-viewer-off.json
```

For the viewer-on comparison, repeat after a fresh power cycle, unlock and
idle period. Use the same file size and equivalent starting filesystem state.
Reset counters, then reopen the viewer before copying. Close it after eject
and capture to `copy-viewer-on.json`. Record approximate elapsed copy/eject
and button-response times. No test commands automatically write or delete
files, format disks, unlock the device or reflash firmware.

If deletion is the main issue, capture it separately using the same reset /
operation / eject / capture sequence. Record whether you used Move to Trash
or permanent deletion; they are different workloads.

## What the JSON contains

- Authentication total, separate PBKDF2 and iterated KMAC durations, storage
  activation, and the timestamp of USB storage exposure. The attach marker's
  duration is not meaningful; its timestamp is.
- USB task, input update, main-loop display pass, actual display presentation
  and debug task times; maximum service-start gaps for USB/input/display.
  Input handling may include synchronous authentication. USB task duration
  includes synchronous storage callbacks. These categories overlap.
- Counts of host READ10/WRITE10 callbacks, including partial callbacks. These
  count requests, not successful operations or unique sectors.
- Read-address histogram: 1,024-sector ranges for LBAs 0–65,535, plus an overflow
  bucket for larger LBAs. This helps locate metadata scans without file data.
- Most recent 128 sequential read/write runs with first/last timestamps. Runs
  overwritten by later activity are counted explicitly. The histogram and
  totals still cover the complete period. Partial callbacks are marked.

Times are monotonic since boot; counters cover since boot or the most recent
`--reset`. There are no file contents, keys or entered passwords in the
capture. KDF timings may include setup/provisioning if those happened in the
same boot; use an existing vault for this test.

Instrumentation itself has overhead. Compare runs using the same diagnostics
firmware; we will check conclusions against the non-diagnostic build later.
The headless presentation timings measure framebuffer rendering, not physical
SPI-screen transfer. That transport still needs working screen hardware.

The protocol adds `j` for a snapshot and `k` to reset diagnostics and snapshot.
Both return FVT1, a 32-byte header and a versioned fixed-size word payload.
`--reset` does not reset the older storage-profile counters, keys, disk state,
security attempt counters or password-hardening settings.
