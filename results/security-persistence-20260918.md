# Persistent authority bring-up — 2026-09-18

Board: `317741A1459A6F94`, USB `cafe:4022`, SDK 2.3.0, chip revision 3,
ROM revision 4, CPU 150 MHz, SD 25 MHz. Persistent authority and both debug
capabilities advertised by the flashed image.

## Read-only baseline

`STATE`: open_result -1, boot_recovery -1, journal_result -1. Other zero fields
are unavailable/default output, not evidence of an initialized empty journal.
OTP pages 16–24: each 64 blank rows, zero locks/software locks, no read errors.
Snapshot: [pre-provision](otp-317741A1459A6F94-20260918-pre-provision.json).
No provisioning, OTP programming, flash erase/program or SD test commands were
sent during this run.

The current open error combines several causes; stale data in the reserved flash
area is a hypothesis, not yet a measured finding. Added read-only occupancy fields
and a guarded explicit virgin-board flash preparation command. It refuses any
occupied/unreadable enrollment OTP row and unsupported permissions, and erases
only the reserved 8 KiB. There is no automatic reset/provisioning on boot.

## Updated artifact and verification

`build-pico-security/fuse_vault_security.uf2` SHA-256:
`5ab9b584c66ad34d8a48e71a33beb4a1089e311ff6b7cb07c5fc1d5f261b3254`.
Debug and debug-disabled ARM builds pass. Relevant host tests pass: both persistent
SHA variants and CLI guards. Preparation tests cover all 576 individual occupied
OTP rows, unreadable OTP, idempotent blank flash, interrupted erases/retry and
refusal after provisioning. These are model tests, not physical programming proof.

Next: flash updated image; inspect occupancy; prepare reserved flash only if the
virgin OTP guard permits; provision, create and read test payload; power-cycle,
verify attempts/data, then test destruction and advancement on this sample.

## Updated firmware hardware run — initial provisioning passed

The updated `STATE` returned root_blank=1, tokens_blank=1, flash_blank=0.
This confirms occupied reserved flash prevented virgin enrollment; its previous
contents/origin were not inspected. Explicit guarded `prepare-flash` succeeded,
after which all three blank flags were 1 and open_result was 1.

`provision` succeeded. Journal state: EMPTY, sequence 1, token slot 0.
[Post-provision OTP snapshot](otp-317741A1459A6F94-20260918-provisioned.json)
shows only pages 16 and 17 changed in the reported fields: 17 programmed rows on
the root page and 18 on the token page. All lock fields unchanged. Snapshot
comparison reports aggregate occupancy, not secret contents.

- [Create/write](persistent-create-20260918.json): succeeded, 4.768050 s total.
- [Unlock/read/verify](persistent-check-20260918.json): succeeded, 3.228654 s total.
- Both used OTP-and-flash authority, 60,000 iterations and 1 MiB payload with
  AES/Camellia layers. These timings include lifecycle work, not isolated throughput.
- [One wrong credential](persistent-wrong-20260918.json): rejected as expected
  (result -4), 1.439539 s.
- [Before power cycle](persistent-before-power-cycle-20260918.json): ACTIVE,
  sequence 8, generation 1, attempts 1, pending false, token slot 0.
  boot_recovery=-1 describes the initial boot before flash preparation/provisioning,
  not a new failure after the successful journal operations.

Stopped here for a full physical power disconnect. Next read STATE **before any
successful unlock** to verify attempts remain 1; then unlock/read all test data.
Destruction/next-token tests remain pending. No OTP locks were changed.

### First reconnect check

After the requested power cycle, the board was absent from `lsusb` and no serial
by-id directory existed. The state command failed its USB identity check before
sending an unlock or mutation. Kernel logs showed USB disconnect at 20:52:42 BST
and a USB-C controller error at 20:52:48; the error does not establish the cause.
Persistence remains unverified pending a successful normal USB reconnection.

### BOOTSEL isolation

Manual BOOTSEL enumerated as 2e8a:000f, serial 317741A1459A6F94, bus 5
address 68; RP2350 volume mounted. This establishes functioning ROM USB
communication on this connection, not the cause of normal-firmware failure.
Built USB-enabled picotool in /tmp/fv-picotool-usb from existing source.
Read-only metadata/program-range inspection was blocked by Linux USB permissions
(root:root 0664); noninteractive sudo required a password. No firmware or
authority writes occurred. Await per-device USB access to continue diagnosis.
