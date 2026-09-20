# USB storage pipeline

The MSC transport now uses two 32 KiB buffers: TinyUSB owns the endpoint buffer,
and the storage worker owns an aligned scratch buffer. This replaces the two
4 KiB buffers; the net buffer increase is 56 KiB. The vault's current maximum
batch is 64 sectors (32 KiB), so larger buffers would also require changing that
contract. No volume, credential, OTP or flash-journal format changes are involved.

## Writes

1. TinyUSB receives a chunk into its endpoint buffer.
2. Core 0 copies it to scratch and queues encryption/authentication/SD work on
   core 1. For a non-final chunk, TinyUSB immediately arms the next receive.
3. If the next receive finishes first, its callback returns zero until the worker
   finishes. The endpoint buffer remains intact; scratch is never overwritten.
4. The final chunk's callback returns zero until that write completes. Only then
   can TinyUSB send a successful command status wrapper (CSW).

Acknowledging a chunk to the TinyUSB state machine is not acknowledging success
of the SCSI command to the host. SD or authentication failures fail the command.
Each worker write still performs the existing data/metadata/bitmap synchronization;
there is no write-back cache across commands and no new multi-sector atomicity claim.
The activity counter counts successfully completed worker writes.

## Reads

The first chunk is read, authenticated and decrypted asynchronously. Core 0 copies
successful output into the endpoint buffer, queues the next chunk, and lets USB
transmit the current one. Prefetch is bounded by the validated READ(10) command;
it never reads past the command or vault capacity. Failure output is wiped.

Both paths remain sequential inside the worker: crypto and SD do not overlap with
each other. This change overlaps their combined work with USB transfers. Small
single-chunk commands cannot benefit from inter-chunk overlap.

## Ownership, cancellation and TinyUSB integration

Only core 0 accesses the bridge's async bookkeeping. Exactly one worker batch may
be outstanding. Synchronous control/status operations first drain that batch and
retain its response for the pipeline. Lock, disconnect, bus invalidation and UI
maintenance drain outstanding work before clearing transport buffers. Stale read
results are not returned after invalidation. BOT class reset drains and clears the
pipeline before resetting the MSC state machine.

The SDK's MSC callbacks lack command boundaries, and its write-complete callback
runs after sending status. `firmware/security/msc_command_boundary.cmake` therefore
generates a minimally extended MSC driver at configure time: a validated READ/WRITE
begin hook and a BOT-reset cleanup hook. The SDK file itself is untouched. Its SHA256
is pinned so SDK updates require an explicit patch review. Original licence text
is retained in the generated source. TinyUSB's existing zero-return retry mechanism
handles pending chunks; no custom USB controller driver is introduced.

## Validation and hardware acceptance

Normal and ASan/UBSan host tests cover queue ownership, waiting, final completion,
short tails, errors, invalidation and wiping. The real patched TinyUSB BOT state
machine is also run with a deliberately delayed worker, verifying that the next
receive is armed before SD completion and that CSW cannot report early success:

```sh
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_usb_pipeline_driver.py \
  --sdk /home/adam/pico-sdk --firmware-build build-pico-device-ui --sanitize
```

The script exercises 4, 16 and 32 KiB buffers. These are correctness tests, not
hardware throughput measurements. Leak checking is disabled for the sandbox's
ptrace restriction; address and undefined-behaviour instrumentation remain active.

After flashing, unlock the existing vault, copy a sizeable file and compare its
checksum after eject/unplug/reconnect. Measure writes including host flush time,
and reads after reconnect to avoid measuring host cache. Repeat with the same
cipher stack, card and file as the old firmware. Check UI/lock/reconnect and both
USB routes. SD interruption acceptance and worker timeout recovery remain on the
production checklist. A firmware build alone does not establish a speed gain.
