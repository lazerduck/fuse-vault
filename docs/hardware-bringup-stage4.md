# Stage 4 hardware evidence and bench checklist

Status: portable safety boundaries implemented; physical bring-up blocked on
reviewable CAD exports and assembled revision-1 hardware.

## Source audit

The sole CAD source is `circuit board/fuse-vault.eprj2`, SHA-256
`6f312cc7bd64cec8153562116cc56b969398c859f1d0831f089b2fd661811e82`.
It is an EasyEDA Pro SQLite project, not a reviewable manufacturing export. A
read-only SQLite inspection found project UUID
`d766685e51af05941c956e9563c4dbcccaa807d16956375de993661403eb598e`,
branch UUID `f78d6e975ac543c2aa79a7ca9c02f6cc`, structure ticket `40903`, Board1 UUID
`ceac7a9aaf64ff7e`, Schematic1 UUID `b9574223088d14dd`, sheet P1 UUID
`6a9d8f4f3592a273`, and PCB1 UUID `44c27b65225b0f26`. The current schematic and
PCB payloads are encrypted history blobs; component, device, document, copper,
and text tables contain no independently reviewable current design data.
Consequently no controller identity, net, polarity, mux truth table, BOM value,
or PCB rule result can be confirmed from this environment.

Firmware identifies this configuration as board revision 1, but the pin aliases
remain claims awaiting review. Three compile-time evidence gates in
`firmware/boards/fuse_vault.h` remain zero: USB mux truth table, SD card-detect
polarity, and display controller. They may be changed only in the same reviewed
change that records the corresponding export and bench evidence.

## Required deterministic CAD export set

Export from the exact source hash above, without print timestamps where the tool
permits, and record the EasyEDA Pro application version:

- searchable schematic PDF plus a machine-readable netlist;
- BOM CSV with reference, value, manufacturer part number, footprint, and DNP;
- PCB Gerber/ODB++ and drill files, pick-and-place CSV, layer-stack description,
  top/bottom assembly drawings, and high-resolution copper renders;
- ERC and DRC reports with zero unexplained errors and the exact ruleset;
- a manifest containing SHA-256 for every export and the board/PCB revision.

A peer reviewer must reconcile every alias in `fuse_vault.h` with the netlist and
sign off the duplicated GPIO2/16 and GPIO3/17 presence connections, power paths,
pull resistors, ESD devices, and RP2354A package/stacked-flash selection.

## Fail-closed implementation contract

`fv_display_t` separates controller transport from application rendering. It
initializes explicitly, suppresses identical frames, rate-limits changed frames,
and becomes unusable after a present failure. There is no RP2354 display backend
because controller commands, reset/backlight levels, SPI limit, rotation, and
colour order are unconfirmed.

`fv_connector_safety_t` requires mux-disable to be the first hardware operation,
then configures presence pins as inputs. Conflicting A/C presence or a read error
re-disables the mux and latches a fault. It deliberately exposes no mux-enable or
select API until the truth table and power policy are confirmed.

`fv_removable_block_t` wraps a raw 512-byte block device with independent card
detect. Removal, backend not-ready, or backend I/O failure latches not-ready and
calls the application fault sink once. Range, argument, read-only, and integrity
errors remain distinguishable. There is no claimed SDIO backend: pin grouping,
PIO program, detect polarity, clocking, pull-ups, DMA behavior, and hot-removal
electrical safety are unresolved.

## Bench checklist

Use a current-limited supply, an unprovisioned board, and a disposable SD card.
Do not enable OTP provisioning or attach valuable media.

1. Record board serial/revision, firmware commit, CAD-export manifest, supply
   voltage/current limit, instruments, and SD make/model/capacity.
2. Hold reset and power-cycle with USB-A only, USB-C only, both, and neither.
   Probe mux OE/select and D+/D-. Confirm both data paths remain disconnected;
   capture labelled logic-analyser traces. Confirm GPIO16/17 remain high-Z and
   quantify any contention on all four presence GPIOs.
3. Derive the mux truth table by schematic review, then confirm every state with
   continuity/probing before setting the firmware evidence gate. Verify both-
   connector and back-power policy under plug/unplug order permutations.
4. Measure presence inputs for both connectors, open/connected thresholds and
   bounce. Exercise at least 100 insert/remove cycles and confirm conflict/read
   failures drive disabled mux plus application fault.
5. Identify the display controller from the populated part/module and datasheet.
   Confirm logic levels, reset/backlight polarity, SPI mode/rate, dimensions,
   orientation and colour order. Render solid red/green/blue/white/black,
   checkerboard, corner labels, and a one-pixel border; capture reset and first
   frame transfers with a logic analyser.
6. Exercise every navigation button individually and in combinations, including
   hold/repeat and reset-time states. Confirm active polarity and external pulls.
7. Confirm SD bus width, pin assignment, pull-ups, detect polarity, voltage and
   maximum clock from schematic and measurements. Insert/remove at idle and
   during read, write, and sync; every removal must make later operations not
   ready and put the application in fault/lock.
8. On a disposable card, record CID/CSD-derived geometry, then perform at least
   100 bounded passes over a declared scratch LBA range: deterministic pattern
   write, sync, power cycle, read, byte-for-byte verify. Repeat with injected
   command timeout, CRC error, and removal. Never address outside the recorded
   scratch range.
9. Re-run reset/fault mux probes after every peripheral failure. Archive traces,
   serial logs, test output, and hashes beside the export manifest. Peer review
   all results before changing any confirmation gate.

## Host verification

Configure and run `firmware/host` with CMake. The
`fuse_vault_stage4_peripheral_safety` test covers display and connector
initialization failure, render scheduling/failure, mux-disable ordering,
connector conflict, card removal, raw I/O failure, one-shot fault latching, and
application USB-detach/session-erasure commands.
