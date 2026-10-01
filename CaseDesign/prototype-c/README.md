# V2 case — prototype C: assembly access

Revised after the first populated V2 board trial: the overall fit was close, but
the PCB groove, USB-C bridge and SD-card recess obstructed installation. The old
internal nut was also awkward. A/B models and ZIPs are preserved unchanged.

**Print C base and C lid together.** Reuse the existing USB-C collar, D-pad cap,
USB covers and nominal M2 x 10 screw / 4 mm AF x 1.6 mm M2 nut. Those hardware
sizes remain assumptions: check the actual screw and nut before tightening.

## Changes

- PCB now lowers from above onto the six existing bottom supports. The narrow
  enclosed edge groove is replaced by an open seat with 0.6 mm clearance on each
  XY edge. A 0.3 mm wider lead-in at the top helps guide it in. Board seating height
  is unchanged, preserving screen and control positions.
- USB-C collar seat is open from above. Put the existing collar over the plug on
  the bench, then lower it together with the board. The lid captures the collar
  on closure. Its successful connector bore and outer geometry are unchanged.
- Enlarged SD clearance continues upward to the open seam: X 50.7–64.1,
  Y -14.8–2.5, bottom Z -2.7 mm. This gives the assumed card envelope about 1.2 mm
  extra room each side, 1.3 mm at its outward tip, and 1.05 mm underneath. Insert
  and latch the card before lowering the board. It remains covered in use;
  removing the lid and lifting the board gives service access.
- M2 nut drops into a hex recess **from the outside/top after closing the lid**.
  No hidden captive-nut installation. The 4.6 mm AF pocket has a lead-in, a
  supporting floor at Z 6.4, and a through hole of diameter 2.6 mm. A nominal
  1.6 mm thick nut sits 0.2 mm below the lid. Screw goes in from the underside.
  Revised head seat at Z -2.0 lets nominal M2 x 10 reach the top of that nut.
- Body is **1.2 mm longer at the USB-A end** to leave a roughly 1 mm end wall
  around the new PCB clearance. Overall body is now 77.3 x 30.6 x 13.8 mm.
  The same USB-A cap seats 1.2 mm farther out; its shape is unchanged. Nominal
  exposed USB-A length is 13.95 mm with the cap removed. Check actual USB mating.

No change to the B flat lid concept, wide TPU D-pad, integral back-button tab,
USB-C collar bore, or external side profiles where the end caps grip.

## Assembly order

1. Remove both USB end covers and the joystick cap. Keep the lid off.
2. Fit/latch the SD card on the board while it is accessible.
3. Slide the USB-C collar onto the plug: narrow nose outward, flange toward PCB.
4. Lower the **populated board, fitted SD card and collar together**, level, into
   the open base. Both USB plugs lower into their openings. The collar flange
   lowers into its U-shaped seat. Confirm all board support pads are reached;
   do not use the screw to pull an obstructed board into place.
5. Arrange the display and flex, engage the original lid hooks at the USB-C end,
   and lower the lid. Check that the back button returns freely.
6. Drop the nut into the top hex recess. Insert the M2 x 10 screw from below and
   tighten gently. No need to hold a nut inside the case. Check the screw reaches
   the full nut thickness without projecting above the lid; hardware can vary.
7. Fit the TPU D-pad and USB covers. Check control movement and USB mating.

The screen/flex route remains provisional; allow access to it during assembly.
If the real SD card extends beyond this assumed envelope, measure the protruding
edge before forcing it. The previous modelled card outline was never a physical
measurement of the new board.

## Printing and files

- `base.stl`: floor down, already oriented. Open PCB/collar/card entries need no
  roof supports. Use the PLA shell settings that worked for the previous case.
- `lid.stl`: outer face down, already oriented. The nut recess is open at the bed
  and closes to the small screw hole with a short bridge. Inspect that bridge
  in the slicer; block supports in the back-button slots and motion gaps. Existing
  hooks may need the same local support treatment as the previous lid.
- Accessory STLs are supplied for a complete set, but need not be reprinted.
- `back_button_coupon.stl` is the unchanged B test piece, optional if already tested.
- STEP parts retain assembly coordinates. `case_with_board.step` contains reference
  electronics; `case_with_hardware.step` contains nominal, unthreaded hardware
  envelopes. Neither assembly is a single printable part.
- `preview.png` shows the actual CAD and loading order.

## Validation and limits

`assembly_validation.json` checks conservative continuous upward sweeps of the
PCB outline, collar exterior and enlarged SD clearance through the base. It also
samples the full populated-board insertion, including solder/peg/terminal and
flex envelopes, collar installation over the plug, nut entry from above and screw
entry from below. Samples do not prove arbitrary hand movements or tolerance fit.

`validation.json` checks final component clearances, sampled lid closure/control
motion, reusable end covers, valid solids, closed STL meshes and STEP round trips.
Intentional TPU grip interference is recorded separately. A/B baseline hashes are
in `baseline_preservation.json`.

**C is not physically tested.** CAD uses simplified components and provisional
card/flex positions. Prior B back-tab stiffness, fatigue and actual actuation are
still unconfirmed unless tested separately. A wider D-pad remains exposed above
the lid. New board availability does not validate every earlier assumption.

Rebuild in the repository with CadQuery 2.8 and VTK:

```sh
python CaseDesign/build_case_c.py
python CaseDesign/render_case_c.py
```

Dimensions are in `P` in `build_case_c.py`; it reuses the A/B generators. Changes
are written only to `prototype-c/`. Repackage the ZIP after rebuilding.
