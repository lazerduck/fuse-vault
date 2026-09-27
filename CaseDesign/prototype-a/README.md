# V2 fit prototype A

First physical-fit iteration, not a production enclosure. Designed for the supplied
V2 reference: two locating hooks, one M2 screw, captured USB-C collar, enclosed SD,
square-socket joystick cap and guided back-button plunger.

## Files

- `base.stl`, `lid.stl`, `usb_c_collar.stl`, `joystick_cap.stl`, `back_plunger.stl`:
  separate printable parts, millimetres, already oriented on Z=0.
- Matching individual STEP files: editable solids in assembly coordinates.
- `case_assembly.step`: the five printed parts in their assembled positions.
- `case_with_board.step`: assembly including corrected simplified board reference.
- `preview.png`: renders of actual CAD solids.
- `validation.json`: solid validity, STEP round-trip, collision and sampled closure checks.
- `reference/`: revised mechanical reference, separate from original revision 1.

The shell is 76.1 mm long, 26.2 mm wide through its narrow section and 30.6 mm
at the single-screw shoulder. Body height is 13.8 mm; joystick cap brings maximum
height above the base to 18.2 mm. Collar extends the shell length to 78.1 mm.
These dimensions exclude exposed USB plugs.

## Materials and first print

Use PLA for base, lid, collar and plunger. Use TPU for the joystick cap as requested;
its 2.6 mm square socket is a trial fit on the drawing's nominal 2.5 mm shaft.
ABS is not required for this iteration. Do not force a tight collar or cap onto
the soldered parts; adjust the parameter and reprint the small part instead.

Print the collar and cap first to minimise wasted filament. The collar should slide
over the exposed plug and seat around its rear shell. Check that a real USB-C
receptacle can still engage fully before printing the whole case. Its front edge
leaves 7.257 mm of the nominal plug exposed; this is not a certified mating depth.

For an assumed 0.4 mm FDM nozzle, start with 0.16-0.20 mm layers and four walls for
the rigid parts. Keep the supplied STL orientations: base bottom down, lid outer
face down, collar flange down, cap top down, plunger button face down. Inspect the
slicer for local supports under the base's plunger-stop cantilever and hook-pocket
roofs. The lid hook tips may also need local support. All supports must be removed
before fitting the board. These are starting settings, not a printer-specific profile.

Hardware allowance: one M2 x 10 mm screw with a head no wider than 4.2 mm and no
taller than the 2.8 mm recess; one nominal 4 mm-across-flats, 1.6 mm-thick M2 nut.
Check actual hardware before printing. Nut pocket is 4.35 mm across flats. Tighten
only until the seam closes; case posts provide the stop, not the PCB or display.

## Assembly

1. Fit the SD card. Position the display in the PCB guide with its flex toward the
   cutout. The model assumes 0.5 mm of mounting gap/adhesive and a 1.6 mm maximum
   module thickness. Display attachment and folded flex must be checked physically.
2. Slide the collar over the USB-C tip. Lower board and collar together into the
   base; the collar flange drops into the end pocket and the PCB rests on six ledges.
3. With the lid inverted, put the nut in its hex pocket and insert the back plunger
   from inside. Hold these loose pieces in place while offering the lid to the base.
4. Engage both USB-C-end tongues at a shallow angle, then lower the USB-A end.
   Sampled closing positions from 0 to 15 degrees are checked in the model. Do not
   bend the hooks to snap them into place. Confirm the flex and plunger are free.
5. Insert the screw from underneath. Fit the TPU cap over the square shaft and
   check all directions, centre press and the back button without binding.
6. Check both USB plugs in real receptacles. Two small underside pinholes provide
   access to the board's service switches; the SD card has no external opening.

## What remains provisional

- Printed fits, strength, screw-head dimensions and nut fit are not physically tested.
- USB-C height and seating datum use its drawing; rear details remain simplified.
  USB-A seating, underside solder lengths and SD card protrusion still need checking.
- Joystick drawing confirms 9 +/-0.1 mm total height, 2.5 mm square shaft, directional
  switch stroke 0.2 +/-0.1 mm and centre stroke 0.15 +/-0.1 mm. The 1 mm lateral
  shaft-clearance allowance is conservative, not a derivation of tilt kinematics.
- Back-button plunger has 0.25 mm initial gap and 0.55 mm maximum case travel;
  its switch's actual travel is not verified. Do not press harder if it fails to click.
- Display active-area offset is modelled assuming its flex faces the cutout. The
  provided datasheet's drawing and summary differ slightly in overall dimensions.
- Collision checks use simplified parts. No stress/thermal analysis or continuous
  insertion-path certification is claimed. The screen is adhesive-mounted, not
  clamped between the case halves.

## Regeneration

From `CaseDesign`, use a Python environment with the parent `requirements.txt`:

```sh
python build_case.py
python render_case.py
```

Change `prototype_parameters.json` for fits, materials, collar, cap, screw and main
dimensions. The script preserves the original STEP/STL reference. Never treat old
validation output as current after editing parameters: rebuild before printing.
