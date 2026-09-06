# Stage 4 hardware evidence and bench checklist

Status: full schematic received 2026-09-06; physical bring-up awaits assembled
revision-1 hardware.

## Full schematic reference, 2026-09-06

User-supplied `SCH_Schematic1_2026-09-06.pdf`, sheet P1, SHA-256
`42860e6bbdf8ebfded6feb0727c9a225649bd29e2bba9ad655350791a23c94e5`,
was reviewed visually from `/home/adam/Downloads/SCH_Schematic1_2026-09-06.pdf`.
This supersedes the earlier lack of readable schematic evidence below.

- USB-A presence uses R4=100 kOhm from VBUS and R7=120 kOhm to ground;
  USB-C uses R5=100 kOhm and R6=120 kOhm. Both assert high (nominal
  2.73 V at 5 V VBUS), and their firmware polarity is now recorded.
- SD_CD on GPIO24 has R2=10 kOhm to 3V3. CARD1 does not depict the
  mechanical switch state. The user's tentative low-when-empty expectation
  is recorded; measure empty and inserted states before selecting its level.
- The user confirms horizontal 160x80 screen mounting with the flex through
  the PCB. Exact rotation, RAM offsets and colour order await the first image.
- Q1 AO3401A is the high-side P-channel LEDA switch, with R27 pulling its
  gate to 3V3. Firmware now drives the gate high during display initialization
  and low to enable the backlight.

The USB control and presence polarities are established from the schematic;
SD detect and display initialization remain gated for the first board test.

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
Consequently no complete netlist, populated BOM value, or PCB rule result can be
confirmed from the repository alone. A supplied revision-1 schematic excerpt
does establish an FSUSB42MUX with 10 kOhm pull-downs on OE# and SEL. The board
maps SEL-low/HSD1 to USB-C. Consequently the passive reset state enables the
USB-C data path for ROM firmware flashing; application firmware drives OE# high
before initializing any other peripheral. The manufacturer's truth table
confirms OE low is enabled, SEL low selects HSD1, and OE high disconnects both routes:
https://www.onsemi.com/download/data-sheet/pdf/fsusb42-d.pdf. A public datasheet for the
documented N096-1608TBBIG09-C08 display module identifies an ST7735S controller,
80x160 pixels, four-wire SPI, active-low reset, and active-low chip select:
https://assets.ebee.com/datasheet/2311032116_Newvisio-N096-1608TBBIG09-C08_C18723032.pdf.
The assembled part still has to be reconciled with the production BOM and
measured on the board.

Firmware identifies this configuration as board revision 1. Mux control and
USB presence polarity are recorded. SD card-detect polarity and the display
controller initialization profile remain gated pending bench evidence.

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
and becomes unusable after a present failure. A default-on RP2354 ST7735S
backend now performs reset/init and sends the common framebuffer as RGB565 over
SPI. Its landscape orientation, controller-RAM offsets, backlight polarity and
8 MHz rate are explicitly candidates, not production facts. The controller's
RGB565 mode is defined by the ST7735S datasheet:
https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf.

`fv_connector_safety_t` requires mux-disable to be the first application hardware
operation, then configures presence pins as inputs. The RP2354 backend now drives
OE high first, keeps duplicate presence GPIOs high-impedance, routes exactly one
observed connector, and re-disables/latches a fault if presence changes or the
two connectors conflict. Routing stays unavailable until the presence assertion
levels are confirmed; the full schematic now provides those levels.

`fv_removable_block_t` wraps a raw 512-byte block device with independent card
detect. Removal, backend not-ready, or backend I/O failure latches not-ready and
calls the application fault sink once. Range, argument, read-only, and integrity
errors remain distinguishable. A baseline target backend now uses the routed
CLK/CMD/DAT0/DAT3 pins in SD-card SPI mode, including command CRC7, data CRC16,
bounded initialization, CSD capacity parsing, single-block I/O and sync/status.
It deliberately makes no four-bit-SDIO performance claim: detect polarity,
pulls, card compatibility, throughput and hot-removal electrical safety remain
bench items.

## Bench checklist

Use a current-limited supply, an unprovisioned board, and a disposable SD card.
Do not enable OTP provisioning or attach valuable media.

1. Record board serial/revision, firmware commit, CAD-export manifest, supply
   voltage/current limit, instruments, and SD make/model/capacity.
2. Hold reset and power-cycle with USB-A only, USB-C only, both, and neither.
   Probe mux OE/select and D+/D-. Confirm the pull-down default routes USB-C,
   then confirm field firmware drives OE high as its first application hardware
   action. Capture labelled logic-analyser traces. Confirm GPIO16/17 remain
   high-Z and quantify any contention on all four presence GPIOs.
3. Confirm SEL-low reaches USB-C/HSD1 and SEL-high reaches USB-A/HSD2 with
   continuity/probing. Verify OE-high disconnect, both-connector and back-power
   policy under plug/unplug order permutations.
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
