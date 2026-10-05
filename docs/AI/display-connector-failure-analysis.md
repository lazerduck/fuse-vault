# Revision-1 display connector investigation

Inspected 2026-09-14 after smoke at the screen flex-to-glass junction.

## Findings

The saved PCB project preserves the schematic's U3 pad-to-net assignments.
It records U3 moving from the top layer to the bottom layer, without changing
those assignments. The photographed screen insertion appears to mate screen
contact 8 with PCB pad 1, through screen contact 1 with PCB pad 8. This would
reverse the display supply. Confirm the physical mapping by unpowered
continuity measurements before any repair or further powered screen test.

Bottom-side placement is not intrinsically an electrical error. Nor does
moving a correctly defined component between layers swap its logical nets.
The important mismatch is the assumed one-to-one numbering between the generic
socket footprint and this particular screen flex when physically mated.
The history does not establish that the earlier top-side arrangement would
have mated correctly, or that the layer change alone introduced the mismatch.

## Project evidence

Source: `circuit board/fuse-vault.eprj2`, SHA-256
`6f312cc7bd64cec8153562116cc56b969398c859f1d0831f089b2fd661811e82`.
The project was opened read-only as SQLite and was not edited.

Its ordinary documents/components tables are empty, but its history records
can be decoded using the keys stored in the same project. All 149 history-data
rows authenticated and decompressed successfully. The read-only decoder used
AES-GCM, each row's hex UUID prefix as IV, its matching history key, then gzip
decompression. Format reference:
https://github.com/dao-genesis/Dao-PCB-Design-Agent/blob/main/lceda_bridge/cdp_studio/eprj2_codec.py

U3 is component `ec90907c7ad7b679` in PCB `44c27b65225b0f26`, linked to footprint
`b71b10e9dd10b0db` and device `ef96e276abfd6d99`. The device identifies C5373445,
SHOU HAN 0.5-8P CTSJ-H2.0 119, with top contacts.

Selected saved component records:

| History-data row | Ticket | Layer | Angle | Position (native coordinates) |
|---|---:|---|---:|---|
| 103 | 11172 | 1, top | 0 | 3786.055, -3875.2184 |
| 107 | 12214 | 1, top | 270 | 935, -345 |
| 108 | 12354 | 2, bottom | 90 | 955, -330 |
| 115 | 21967 | 2, bottom | 90 | 915, -455 |

Row 115 contains the latest U3 placement and pad-net records found in this
saved project. The pad assignments are unchanged across all six saved U3
placement records (rows 103, 107, 108, 109, 112, 115):

| PCB U3 pad | Net |
|---|---|
| 1 | LEDA |
| 2 | GND |
| 3 | TFT_RST |
| 4 | TFT_DC |
| 5 | TFT_MOSI |
| 6 | TFT_SCK |
| 7 | 3V3 |
| 8 | TFT_CS |
| 9, 10 | GND, mechanical anchors |

The stored footprint has pads 1 to 8 in increasing local X order, from
-68.9 to +68.9 at local Y=37.4. No reversal of those pad numbers was found.
Selected extracted records are preserved in
[u3-project-records.json](hardware-evidence/u3-project-records.json).
This is a focused connector audit, not a full PCB connectivity or DRC audit.

## Physical evidence and inference

The user's PCB screenshot has LEDA/R25 at one end of U3 and C13/3V3 toward the
other end. In the actual board photograph, R25 is above U3 and C13 below it;
the screen flex shows 8 above and 1 below. The screenshots must therefore be
compared with the board-side viewing reversal accounted for.

With the apparent mating order, screen ground (contact 2) meets PCB pad 7
(3V3), and screen VDD (contact 7) meets PCB pad 2 (GND). Thus VDD relative to
screen ground would be -3.3 V. This is a strong explanation for the reported
damage, but neither rail voltage nor continuity has yet been measured.

The screen datasheet identifies pins 1–8 as LEDA, GND, RESET, RS, SDA, SCL,
VDD, CS. Its VDD operating range is 2.5–3.3 V, with absolute minimum -0.3 V.
The supplier connector drawing identifies a top-contact socket. Having gold
facing its contacts establishes contact engagement, not matching pin numbers.

Source attachments inspected in this conversation:

- Screen: `/home/adam/Downloads/8248e3d07e4da27b04062054a843a030.pdf`.
- Connector: `/home/adam/Downloads/75add602f4fdc20b628ec2d91ab8ed8d.pdf`.
- Schematic: `/home/adam/Downloads/SCH_Schematic1_2026-09-06.pdf`.
- PCB screenshot: `/tmp/codex-clipboard-d2bd0a20-9e79-4db7-91dd-99883041e060.png`.
- Board photograph: `/tmp/codex-clipboard-c58f0310-bcbe-4de4-ab44-73511e9b4d4f.png`.

## Why nets did not catch it

The schematic models a generic connector with numbered terminals. It instructs
CAD to connect 3V3 to socket pad 7, which the saved PCB does. It does not model
which screen contact physically touches that terminal. Consequently a netlist
comparison can agree perfectly while a mated peripheral receives reverse power.
No claim is made here that a complete DRC run passed.

## Next verification and correction

With all power and the screen disconnected, measure continuity from U3's small
solder terminals to known board ground and 3V3. In the board photograph's
orientation, the second small terminal from the top is expected to be GND,
and the second from the bottom 3V3. Match each to the screen's numbered flex
contacts, rather than relying on an arbitrary left/right description.

If the reversed mating order is confirmed and the same socket and insertion
geometry are retained, the required PCB-pad mapping would be:
1=TFT_CS, 2=3V3, 3=TFT_SCK, 4=TFT_MOSI, 5=TFT_DC, 6=TFT_RST,
7=GND, 8=LEDA; mechanical anchors remain GND. This is a conditional design
mapping, not an instruction to power the existing hardware.

Existing boards may be repairable with a verified reversing adapter or suitable
connector rework. A replacement bottom-contact socket is not automatically a
drop-in fix: footprint, contact geometry, numbering and flex fit must all be
checked. Simply flipping the flex is not a validated repair. Revisions should
represent the screen-to-socket mapping explicitly in the schematic and include
a mating-orientation drawing. Validate repaired supply polarity/voltage before
connecting an undamaged screen; damaged screens cannot validate the repair.
