# V2 enclosure design brief

**Current requirements:** [REVISION_B_REQUIREMENTS.md](REVISION_B_REQUIREMENTS.md)
records prototype-A print feedback and the new joystick protection, back-button
redesign and USB end caps. It supersedes the older control/closure proposals below.
Prototype A exists and has been printed; the remainder of this document preserves
the earlier drawing evidence and planning assumptions, not its current build status.

Updated 2026-09-21 following the user's screen datasheet and enclosure requirements.

## Agreed requirements

- Provide structural support for the long USB-C plug at its narrow PCB attachment.
- Fit a separate joystick cap and a captive plunger for the back button.
- Keep the microSD card inside the enclosure, with no external removal opening.
- Use a two-piece enclosure. The first prototype will use screws for repeatable
  assembly and adjustment; snap closure can be reconsidered after fit checks.
- Locate the PCB with selected clear edge supports and matching lid restraints.
  Case screws join external-to-PCB bosses, not the occupied connector locating holes.

## Drawing evidence

Local copies of the manufacturer PDFs are in `datasheets/`. Source URLs and
SHA-256 digests are in `datasheets/manifest.json`.

### Display: N096-1608TBBIG09-C08, revision A

PDF page 5, Mechanical Drawing, specifies:

- Backlight/module outline: 27.9 +/-0.15 by 13.5 +/-0.15 mm.
- Thickness: 1.5 +/-0.1 mm; provisionally reserve 1.6 mm before mounting adhesive
  and assembly clearance.
- Active area: 21.7 by 10.8 mm. This is offset along the long axis, not centred
  within the module: 1.7 mm from the end opposite the flex, leaving 4.5 mm at
  the flex end. Across the width the nominal margins are 1.35 mm.
- Flex extension from the module end: 23.0 +/-0.5 mm in the flat drawing.
- Flex contact end: 4.5 +/-0.07 mm wide; 0.3 +/-0.03 mm thick including stiffener.

Page 4's summary instead lists a 27.95 mm module length and 1.5 mm maximum
thickness. Retain this discrepancy: use the dimensioned drawing plus its
tolerances for provisional clearance, and verify the supplied module before
finalising its seat. The existing 27.9 by 13.5 silkscreen guide agrees with the
drawing's nominal outline. Its existing 2.5 mm thickness is an obsolete assumption.

The expected flex exit faces the PCB cutout (negative X), but installed orientation
and folded route must be checked. With that orientation, the active area would
start 4.5 mm from the module's negative-X edge and end 1.7 mm before its positive-X
edge. Do not centre a bezel opening on the module automatically. Provide mounting
support under the module perimeter/backlight structure without point loading the
glass or pinching the flex. The flat flex length does not establish bend radius.

### USB-A: SHOU HAN AM90

Manufacturer drawing on PDF page 1:

- Main shell height: 4.50 +/-0.10 mm.
- Mating shell width: 12.00 +/-0.10 mm.
- Rear/attachment width: 12.52 +/-0.15 mm.
- Body length shown: 18.75 +/-0.15 mm.
- Rear view also shows a 5.85 +/-0.10 mm overall extent including attachment
  features; 4.5 mm is not an all-features envelope.

The current 4.5 mm shell height is therefore supported by the drawing. The model
still needs a separate mating shell and rear attachment envelope, plus registration
of the seating plane against the PCB footprint. Its simple 12.4 mm wide box is
not an accurate representation of both regions.

### USB-C: XKB U261-121N-4BS2S

Manufacturer drawing, PDF page 1, revision A1:

- Main shell height: 2.40 +/-0.03 mm.
- Main shell width: 8.25 +/-0.05 mm.
- Rear width: 8.75 +/-0.25 mm; front view also shows an 8.60 mm extent.
- Overall length: 13.61 +/-0.25 mm.
- Side view shows 11.25 mm to the rear of the principal shell.
- Rear view places the solder-contact plane 0.95 mm above the shell bottom.
  Interpreting that plane as the PCB top gives nominal shell bounds Z=0.65 to
  3.05 mm for a 1.6 mm PCB, before solder seating variation. Confirm that datum
  interpretation and footprint registration before generating the close-fitting cradle.

The current reference instead uses Z=-0.4 to 2.0 mm, from an assumed
`bottom_from_surface=-2`. Do not size the support from that reference without
correcting the mounting datum. The 2.4 mm shell height itself is supported.

## USB-C support construction

Use an integrated lower cradle and a matching lid saddle around the rear metal
shell, with side ribs tied into the main case. This provides a load path from the
connector shell into the case, reducing dependence on the narrow PCB attachment.
Provide nearby PCB restraints so the board cannot slide under insertion or
withdrawal loads. Avoid bearing on pins, solder fillets, or small surface parts.

The shroud must stop behind the plug's required mating region. Neither the drawing's
overall length nor the shell height establishes how much may safely be covered.
Keep its length, shell clearance, and shell-to-PCB offset adjustable and verify full
insertion in a real receptacle. Do not assume a long cosmetic sleeve provides support.
The first fit sample should include the cradle, its lid saddle, and the local PCB
supports, so both connector seating and receptacle clearance can be checked together.

## Controls and retention

- Joystick: allow a provisional 3 mm displacement from neutral in any lateral
  direction (conservative interpretation of the user's estimate). This is not a
  verified travel specification. Clearance at the lid depends on shaft height and
  cap geometry; include a separate allowance for centre-press travel. The existing
  10 mm motion diameter only approximates shaft clearance and does not reserve a cap.
- Back button: use a guided, captive plunger with a retaining flange, a small
  unpressed gap, and a mechanical travel stop. SW4 is the provisional target; confirm
  its function before freezing the lid. Neither the required stroke nor plunger
  length is established solely by the switch's total height.
- SD: install the card before case closure. Remove the external withdrawal corridor
  from the enclosure's design constraints, but retain an installed-card envelope and
  a gap that avoids pressing the push-push mechanism. A covered card is not the same
  as a modelled card: the current reference represents only its socket.
- Retention: locate supports using both PCB faces. Match the upper restraints to
  lower supports so screw tightening does not bend the board. Use case-to-case stops
  to control closure; the display must not serve as a clamping spacer.

## Reference status and next construction step

This brief supersedes the old display, USB verification, and externally removable
SD assumptions. Existing `parameters.json`, STEP/STL exports, and `validation.json`
remain reference revision 1; they have not been regenerated by this evidence review.
They must not be described as already corrected or validated against these drawings.

Before building the fit-test tray, update and regenerate the reference with the
screen drawing envelope, separate USB shell/attachment envelopes, corrected USB-C
datum, installed SD card allowance, and cap movement envelope. Then build the
screw-closed tray/lid with the USB-C cradle as a required feature. Printer/material
tolerances and the physical screen/flex installation remain prototype fit checks.
