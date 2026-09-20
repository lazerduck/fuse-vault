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

### Firmware readback and normal restart

After per-device USB access was granted, picotool recognized the installed ARM
Secure image. The 146,688-byte firmware BIN matches flash at 0x10000000 byte for
byte (zero differences). BIN SHA256:
`09b31f59fe8864ddc511b63c31c7f72eada11ab6e1f5dce7c6772d67f42e9c83`.
Only range 0x10000000–0x10026000 was read; journal and OTP secrets were excluded.
A bootloader-triggered restart of the unchanged image also failed to enumerate.
This does not identify the cause; the enrolled attempt/data persistence is still
unverified because no normal-firmware commands can be sent.

Prepared separate `FV_DEBUG_STARTUP=ON` image to expose USB before explicit worker
launch, with BOOT/START diagnostics. Normal and diagnostic builds pass. Confirmed
UF2 payload excludes the reserved journal banks. Next action requires BOOTSEL and
flashing this diagnostic image; no reprovisioning or journal reset is intended.

### Startup diagnostic first response

After the user flashed the diagnostic, BOOT succeeded at stage 0 with
start_requested=false, USB-A sense/duplicate 0, USB-C sense/duplicate 1.
Core0 USB therefore works before worker launch. START was rejected by automatic
approval review before execution because authority recovery can complete pending
OTP destruction. Last observed durable state was sequence 8, attempts 1, pending
false; it has not yet been re-read after restart. Worker launch and persistence
verification remain pending explicit approval of that recovery behavior.

### Authorized START, persistent readback and destruction

The user explicitly approved START and its possible irreversible recovery action.
START returned stage 0/requested=true; subsequent BOOT reached stage 14 with USB-C
sense 1 and USB-A 0. Delayed worker launch succeeds; exact normal-start failure
remains unresolved. A diagnostic cold power-up is still to be tested.

[State before unlock](persistent-after-restart-state.json) retained sequence 8,
generation 1, attempts 1, pending false, slot 0; boot recovery returned 0.
This verifies attempt persistence through the prior full power cycle and diagnostic
reflash. [Original payload read](persistent-after-restart-check.json) verified all
1 MiB with the original credential (3.744634 s including unlock).
[Credential change](persistent-change.json) and
[replacement credential read](persistent-replacement-check.json) succeeded.

Explicit authorized [destruction](persistent-destroy.json) succeeded. Firmware
verifies each raw all-ones write before committing DESTROYED. The
[destroyed state](persistent-destroyed-state.json) is sequence 19, generation 2,
slot 0, attempts 0. [OTP snapshot](otp-slot0-destroyed.json) differs from the
provisioned snapshot only on page 17: 17 all-ones rows (16 secret + revocation),
2 programmed markers, 45 blank. All lock fields and root-page occupancy unchanged.
Snapshot occupancy alone cannot prove unchanged root bytes; no secret was exported.
The [old vault check](persistent-destroyed-check.txt) was denied in recovery (-5)
before reading SD. SD still contains the old encrypted vault and headers.

Left DESTROYED intentionally. Next: cold boot this diagnostic, START, confirm
persistent DESTROYED and old-vault denial, then provision slot 1 and test fresh
vault creation. This run did not test interrupted physical destruction or brownout.

### Diagnostic cold-start failure

After the next user-confirmed full power cycle, the startup diagnostic did not
enumerate either. BOOT failed USB identity validation before sending a command;
lsusb showed neither cafe:4022 nor a ROM bootloader. Kernel logs showed disconnect
at 21:13:59 and a USB-C controller error at 21:14:03. The diagnostic deliberately
does not launch core1/recover authority until START, so delayed launch is NOT a
verified startup fix. Successful execution immediately after UF2 flashing must
be distinguished from normal flash boot. OTP slot 0 remains last-observed
DESTROYED, sequence 19; no further writes or slot advancement occurred.
Next inspect ROM partition/boot-selection information in BOOTSEL; a stale boot
record is only a hypothesis and no additional firmware change has been made.

### Partition inspection and trace image

Read-only picotool partition inspection on BOOTSEL bus 5/address 71 reported no
partition table. Installed diagnostic IMAGE_DEF starts at 0x10000138 with a valid
block loop; no stale partition table was found. This rules out that hypothesis,
not all boot selection or hardware initialization failures.

Built `build-pico-security-boot-trace/` with automatic five-second BOOTSEL fallback
and nonsecret watchdog checkpoints. Verified every UF2 payload address stays below
0x101fe000, then loaded, verified and executed via picotool. No journal/OTP writes
were requested. Immediate BOOT succeeded at worker stage 0 with correct A=0/C=1
sense values and no prior trace. Worker has NOT been started in this image.
Next full power cycle will test fallback and preserve its checkpoint if execution
reaches the early hook. User should leave power connected after that test.

### Watchdog trace readout and completed slot advancement

Automatic BOOTSEL return reported slot 0 and diagnostic word 0x500d. Re-executing
identical firmware via picotool update/verify restored USB. BOOT exposed the old
trace magic/stage 1, but two other scratch fields were unexpected (0x10000000 and
25032). Those fields are unreliable: do not infer GPIO state or reboot API status
from them. Stage 1 suggests early SDK initialization, but the exact failing call
is not established. Refined trace now packs magic/stage together in scratch0 and
adds checkpoints after early resets (7) and early USB power-down (8), before the
existing clocks-return checkpoint (2). Linker map confirms hook ordering. The
refined image is built but has not yet been loaded or cold-tested.

The current responsive image then ran authorized START. Journal recovery retained
DESTROYED, sequence 19, slot 0 after cold power removal. Old replacement-credential
vault access was denied (-5) before SD access. Provisioning advanced to slot 1,
page 18, EMPTY sequence 20. New vault create/write and unlock/read of 1 MiB passed
at 60,000 iterations. Snapshot comparison shows only page 18 changed relative to
the destroyed-slot0 snapshot; root page, destroyed page and all locks match in
reported fields. This demonstrates root remains usable without exporting it.
Physical interrupted burns, exhaustion and normal automatic cold startup remain
unverified. Current live board contains the new active slot1 test vault.

### Refined checkpoint: SDK clocks

After the user's cold cycle and automatic BOOTSEL return (bus 5/address 78),
warm execution of the identical image exposed valid tagged stage 8. Linker order
places stage 8 after `runtime_init_usb_power_down`, immediately before
`runtime_init_clocks`; stage 2 is immediately after that call. Thus the observed
stall is in clock initialization, before main/worker/authority. Current A/C sense
is 0/1 after warm execution; no cold sense measurement is inferred.

Added link wrappers around SDK xosc_init, pll_init and vreg_set_voltage in the
trace-only build, preserving original calls. Entry/return stages: crystal 10/11,
system PLL 12/13, USB PLL 14/15, voltage adjustment 16/17. Build passes and linked
clock-init calls target the wrappers. UF2 excludes journal. This refined image
still needs flashing and a cold cycle; no OTP/SD/authority operation ran this turn.

### 2026-09-19: PLLs completed

After user cold boot, BOOTSEL bus 5/address 82 was warm-executed using the identical
trace image. Valid checkpoint 15 means USB PLL initialization returned; crystal
and system PLL calls had also returned. The stall is later in SDK clock setup,
not a PLL lock wait. Worker remained at stage 0, no authority operations ran.

Next trace wraps clock_configure_undivided (entry 20+2*clock index, return +1)
and tick_start (entry 60+2*tick index, return +1), retaining original behavior.
Linked clock-init disassembly confirms calls target wrappers; build passes and
UF2 excludes the journal. Added trace-only REBOOT, accepted only before worker
startup, using ROM normal reboot with slot0 diagnostics. This allows software
restart reproduction; it does not replace final physical cold-start validation.
Next image needs user flash, then leave it running for BOOT/REBOOT testing.

### Software restart of clock-switch trace

After user flash, BOOT showed worker stage 0. Trace-only REBOOT returned 0;
normal diagnostic USB enumerated as device 86, and BOOT retained prior stage 6
(USB mounted), with worker still stopped. The software restart did not reproduce
the cold-start failure on this build. It is not a substitute for a full power
disconnect. Next action: physical cold cycle of this same image to obtain the
clock-switch/tick checkpoint if failure recurs. No authority/OTP/SD commands ran.

### Latest cold cycle: USB descriptor timeouts, checkpoint unavailable

Following the user's cold cycle of the clock-switch/tick trace, Linux detected
new full-speed devices on 5-1 but descriptor reads timed out (-110). Neither
cafe:4022 nor 2e8a:000f was accessible at the initial checks. This differs from
prior successful automatic BOOTSEL fallback; no new checkpoint was retrieved and
no more precise failure location is established. USB-C controller error also
appeared, but is not proof of a host/cable cause. No reset, reflash, OTP or journal
operation was performed during this check. Requested availability of an SWD probe
or spare Pico to inspect the CPU directly rather than continuing speculative
firmware iterations. Last trusted trace remains PLL return stage 15.

### Comparison with the earlier successful benchmark

At the user's request, revisited the previous whole-firmware RAM startup failure
and the successful narrower cipher-only SRAM replacement. Current normal security
build has PICO_COPY_TO_RAM=OFF and PICO_NO_FLASH=OFF, like the successful benchmark;
it does not reintroduce whole-firmware RAM startup. Both use hardware SHA, cipher
SRAM, 25 MHz SD, 150 MHz CPU and the same ordered SDK preinit list.

Extracted object code before linking matches exactly for runtime_init_clocks
(220 bytes), runtime_init_early_resets (52 bytes) and clock_configure_undivided
(208 bytes). This compares code plus relocation placeholders, not final relocated
addresses or proof of identical hardware state. Security firmware adds vault/DRBG/
OTP/flash authority and core lockout, and changes linked memory layout (text
99,892 ->152,828 bytes, BSS 73,464 ->129,484 bytes in compared artifacts). Its
normal worker performs authority recovery at startup; diagnostics defer it, and
the clock checkpoint precedes it.

Recreated UF2 from the preserved benchmark ELF without rebuilding. SHA256 matches
recorded successful 30-case artifact exactly:
b351c00f1329c894b509a7ecfef8bc322c3ca2f795e603004bddb4239590e1f3.
Artifact: build-pico-ciphers-ram/cold-start-baseline.uf2. Verified payload excludes
the journal. Next controlled test is cold boot of this exact baseline with no SD
benchmark/writes. Earlier records prove post-flash operation, but do not contain a
separately recorded cold-start pass of this exact artifact. Current regression
cause remains unproven. No new firmware was flashed during this comparison.

### Exact historical baseline cold-start outcome

Baseline post-flash INFO passed at 150 MHz with hardware HMAC self-test true.
After the user's full power cycle, cafe:4021 did not enumerate; INFO could not
open its port. Linux recorded descriptor read/64 timeout -110. No SD benchmark,
OTP operation or journal write was sent. The security application additions are
therefore not necessary to reproduce this failure on this board/setup. This
comparison does not prove which shared startup/hardware/environment factor causes it.

Inspection found a concrete shared configuration discrepancy: the board header
selects generic_03h boot2, but RP2350 requires PICO_EMBED_XIP_SETUP=1 to execute it.
That option defaults to 0 in SDK crt0, and the historical ELF has equal boot2 start
and end addresses (empty section), no stage2 entry. Thus its conservative flash
setup was not actually embedded. The same omission exists in the normal security
build. Prepared a separate benchmark candidate enabling embedded flash setup,
keeping hardware SHA, cipher SRAM, 150 MHz and 25 MHz SD unchanged. This is a
hypothesis-driven candidate, not yet a demonstrated cold-start fix.

Embedded-XIP baseline candidate build passed. ELF now contains a nonempty boot2
section and crt0 copy/call XIP-setup symbols. UF2 payload is below the journal
region. Artifact: build-pico-cold-start-xip/fuse_vault_v2_bench.uf2; SHA256 6773241dfec0cf800b1b3b0470738fd40041e04d24d702a45e22e37ce7d58d32.
Hardware flash/cold-start validation is pending. Original baseline is preserved.

### Embedded-XIP candidate: user-reported flash and cold cycle

After the user reported ready, elevated lsusb showed neither the benchmark USB
identity nor BOOTSEL; /dev/serial/by-id was absent. Kernel history showed BOOTSEL
on 5-1 at 10:54:59, disconnect at 10:55:20, then a UCSI controller error at
10:57:26, with no subsequent successful application enumeration. Unlike the
previous baseline cycle, no new descriptor timeout was recorded in this interval.
No INFO transaction was possible, so the executing image was not independently
identified and this observation does not localize a CPU startup failure. The
embedded-XIP candidate has not demonstrated a fix. No OTP, SD or journal commands
were issued. Next isolation step: connect normally through another laptop USB
port (preferably another controller), keeping the current firmware unchanged.

### Other-side laptop USB port

User moved the same board to the other side of the laptop. Elevated lsusb still
showed neither application nor BOOTSEL, and no serial port existed. Kernel log
added a UCSI error at 10:58:52 but no new USB device enumeration. Moving ports did
not restore the connection; this does not distinguish firmware startup from
cable/power/host issues. No device commands ran. Next check is BOOTSEL on this
connection, followed by readback verification of the candidate if accessible.

### XIP candidate readback and application reboot

After per-device USB ACL was granted, read back 0x10000000..0x10016f60
(94,048 bytes) from bus7/address2. cmp matched the candidate BIN exactly;
both SHA256: 71535e49f2b87d6bd25cc97659101330bade538d5e81e56e3aa52ffb3c2d9021.
Thus the intended embedded-XIP firmware is installed intact. picotool application
reboot returned success; BOOTSEL disconnected at 11:01:10, but no application USB
or serial port appeared at the subsequent check. The tool's reboot success does
not establish successful application startup. Embedded generic flash setup alone
has not restored operation. No flash writes, OTP operations or SD commands ran.
BOOTSEL and flash readback work through this other-side connection; the failure
remains in the transition to application operation, without an exact fault site.
