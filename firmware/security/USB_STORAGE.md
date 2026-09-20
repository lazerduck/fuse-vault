# V2 USB mass storage and debug sessions

## Scope

Development-only CDC serial + one removable USB mass-storage LUN. USB identity
remains cafe:4022 / Fuse Vault SECURITY DEBUG, with bcdDevice 0x0200 for this build.
CDC uses interfaces 0/1 and endpoints 0x81/0x02/0x82; MSC uses interface 2 and
endpoints 0x03/0x83. FIDO HID is not implemented or advertised yet.

Power-on starts locked. An existing ACTIVE vault is required. Locked media reports
NOT READY / medium absent, with zero capacity. Debug unlock exposes only the
vault's logical 512-byte sectors, never raw SD headers, authentication metadata or
unused physical capacity. This retains the existing volume/key derivations.

The current sample's vault is only 1 MiB and contains a deterministic test pattern,
not a filesystem. Unlocking therefore initially exposes an unformatted block
device. Formatting writes go through the encrypted/authenticated storage path.
Do not enlarge reported capacity beyond the provisioned volume descriptor.

## Build / flash

```sh
cmake -S firmware/security -B build-pico-usb-storage \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DCMAKE_BUILD_TYPE=Release -DFV_USB_MSC=ON -DFV_DEBUG_SESSION=ON \
  -DFV_DEBUG_ENROLLMENT=OFF -DFV_DEBUG_OTP_INSPECT=OFF \
  -DFV_DEBUG_STARTUP=OFF -DFV_DEBUG_BOOT_TRACE=OFF
cmake --build build-pico-usb-storage -j4
/tmp/fv-picotool-usb/picotool uf2 convert \
  build-pico-usb-storage/fuse_vault_security.elf \
  build-pico-usb-storage/v2_usb_storage.uf2 --family rp2350-arm-s --platform rp2350
```

Flash the **new working custom board**, serial 66ED2A91873CF67F, preserving the
original failing sample. Payload is confined below the reserved flash journal.
There is no automatic provisioning, formatting or vault creation. As in normal
V2, boot recovery still processes previously recorded security state.

## Host commands (can run from any directory)

```sh
python3 /home/adam/projects/fuse-vault/tools/storage_session.py --device 66ED2A91873CF67F status
python3 /home/adam/projects/fuse-vault/tools/storage_session.py --device 66ED2A91873CF67F unlock
python3 /home/adam/projects/fuse-vault/tools/storage_session.py --device 66ED2A91873CF67F lock
```

Default unlock uses the existing **public original test credential**. The optional
`--credential replacement` is only for a vault deliberately changed to that test
credential. Incorrect unlocks consume the real attempt budget. No arbitrary
host password or key input is exposed. Repeated unlock while already open is
idempotent and does not reverify credentials or consume another attempt.

Each command verifies USB identity and INFO capability before sending MEDIA,
UNLOCK ORIGINAL/REPLACEMENT or LOCK. Status prints `unlocked`, `blocks`, and
matching Linux disk paths discovered by USB serial. Linux may take time to notice
new media after unlock; run status again if the disk list is initially empty.
No automatic retries are performed. Successful host unlock verifies only session
opening; actual host block I/O must still be tested.

Unmount the filesystem before lock. The helper refuses lock if a corresponding
disk/partition is mounted in its mount namespace. Firmware lock is unconditional:
it cannot know whether another host process still has unsaved filesystem data.
Host eject also locks. Host START/LOAD does not unlock the vault.

### First filesystem

Use the disk path identified for this serial, checking its 1 MiB size with `lsblk`.
For this tiny development volume, FAT12 on the whole disk is appropriate. If the
verified path were `/dev/sdX`, the explicit formatting command would be:

```sh
# Replace /dev/sdX with the actual verified vault disk, never the physical SD size.
sudo mkfs.fat -F 12 -I -n FUSEVAULT /dev/sdX
```

This replaces the prior verification pattern, so the old CHECK ORIGINAL pattern
test will no longer pass. Formatting is not performed by the build or session tool.
After formatting, mount, copy a test file, record its hash, unmount, and lock.
Unlock and verify the file/hash. Repeat after full unplug/replug: it must start
locked and require another debug unlock. Reconnection must not silently reuse keys.

## Ownership and lock behaviour

Core 1 exclusively owns the vault session, keys, SD driver and authority. Core 0
handles TinyUSB/CDC and serializes block requests onto the same worker queue as
debug commands. Separate response queues avoid confusing a USB disk response with
a diagnostic JSON reply. Each request completes before its shared buffer is reused.
There are no independent storage owners or host-side cryptographic keys.

Reads authenticate/decrypt through fv_vault_read; failed reads do not publish
scratch contents. Writes call fv_vault_write, which syncs ciphertext and metadata
before success. SYNCHRONIZE CACHE has no extra dirty data to flush. Invalid LUNs,
partial sectors, oversized callbacks and out-of-range LBAs fail. Authentication
or physical I/O errors lock the session. An unlock transition reports medium
change through UNIT ATTENTION on TEST UNIT READY.

USB reset, unplug/unmount and suspend request lock via an ISR-safe atomic flag.
Only the worker performs key clearing; no blocking work occurs in the ISR hook.
The bridge rejects an I/O result if a bus invalidation arrived during it. Suspend
therefore requires explicit unlock again, including host-initiated autosuspend.
Debug LOCK is serialized after earlier worker requests and clears the session
before acknowledgment. Later sector requests fail. A multi-batch host command
interrupted by lock can fail partway through; writes are not transactionally atomic.

Core-0 scratch is wiped after every transfer, and the known TinyUSB software
buffer is cleared on explicit lock/eject/bus invalidation. Lock cannot retract
plaintext already sent to the host or already handed to USB hardware. Host caches
are outside the device's ability to erase. This is not a claim of secure RAM
partitioning or production firmware hardening.

## Limits / validation

The SDK's bundled TinyUSB has synchronous MSC callbacks (not the async API in
newer TinyUSB releases). This first adapter uses 4 KiB batches and waits on the
worker without recursively calling tud_task. It is a correctness starting point;
USB latency, sustained throughput, host OS compatibility and I/O interruption
must be measured on hardware. A stuck worker also stalls synchronous disk requests.

Desktop callback tests cover locked access, medium change, capacity, 4 KiB writes,
readback, final-sector/overflow boundaries, malformed requests, read/write errors,
buffer clearing, sync and eject. Bridge tests cover reset/unplug/suspend, deferred
ISR handling, and invalidation during a read. Existing vault/authentication tests
remain in the suite. These do not replace real USB host interoperability tests.

Production must remove DEBUG_SESSION and the public credentials and connect
unlock/approval to trusted on-device UI. MSC remains an opt-in build; provisioning,
credential changes and other destructive diagnostics are rejected in this mode.
