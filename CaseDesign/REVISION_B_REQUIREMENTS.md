# V2 prototype B: pocketability and easier printing

Status: requirements and proposed approach, not new printable geometry.
Recorded after the user's prototype-A print feedback. Preserve prototype A and
its exported bundle as the physical comparison baseline.

## Physical feedback from prototype A

- Overall appearance and size look good to the user.
- USB-C collar fits perfectly on the part tested.
- D-pad/joystick cap fits the tested shaft.
- Printed enclosure assembles and closes.
- V2 boards have not arrived: populated V2 board fit, screen/flex installation,
  button actuation and both USB mating clearances are not yet established.
- Joystick appears too exposed for pocket use; snagging could pull off the cap
  or damage the control.
- Back-button pass-through does not print reliably. A ring tore off during support
  removal. Do not assume whether this was the lid guide or base stop ring; eliminate
  the fragile supported ring arrangement as a whole.
- Protective caps are required for both USB ends.

## Requirements

1. Preserve the fit-tested USB-C collar bore and square cap socket. Change them
   only if later board testing gives a reason. Preserve the hook/screw closure and
   compact body envelope as far as practical.
2. Reduce joystick snagging from every pocket-entry direction. A smooth guard
   should meet or slightly exceed the resting cap height, with a finger-accessible
   well and clearance for every direction and centre press. Guard contact loads
   should go into the case, not the shaft. Avoid an exposed cap underside/lip.
3. Back control should be flush or slightly recessed and usable without fragile
   support cleanup. No delicate suspended retaining/stop rings. Preserve tactile
   actuation, prevent preload and provide a robust overtravel stop.
4. Provide separate removable USB-A and USB-C protective caps. Retain them on the
   case, not by loading connector contacts or pulling the captured collar out.
   Provide internal shell/tip clearance, a case-to-cap seating stop and smooth
   pocket-facing surfaces. Removal should not open the case or extract the collar.
5. PLA remains the primary shell material; PETG and TPU are available for specific
   parts. Flexible mechanisms must be validated in the material actually printed.
6. Test small control/cap samples before another full enclosure print. Keep changes
   to already working parts minimal. Do not claim full board fit before V2 arrives.

## Joystick proposal

Prototype A has lid Z=8.2 and cap top Z=12.6: the cap projects 4.4 mm above the lid.
The modelled shaft already reaches Z=10.6. Lowering the cap alone therefore cannot
make it flush with the existing lid; it must retain shaft engagement and roof
thickness. Do not lower the board or cut the shaft to achieve this.

Propose a smoothly ramped local guard surrounding a shallow joystick well, paired
with a modestly lower/rounded TPU cap if roof thickness permits. Preserve the
successful square socket dimensions. Finger access, back-button space, cap tilt,
centre-press travel and local case width must be checked together in CAD.

A separate guard is worth prototyping first: it can be tuned cheaply and preserves
the flat lid printing face. If later integrated into the lid, revisit print
orientation: a raised guard prevents the existing outer-face-down lid lying flat.
Do not solve pocket snagging by creating new support cleanup around the button.
Any separate guard needs positive attachment to the shell and rounded transitions;
friction-only retention next to the joystick is not yet an accepted solution.

## Back-button options

Preferred first experiment: a broad pad on a long cantilever with a rounded root,
defined by a U-shaped slot. A small underside nub reaches the switch. Distribute
bending along the arm instead of creating a thin, sharp hinge. Use a substantial
case stop to limit motion. Size the nub and initial gap against the actual SW4
switch stack; the joystick's travel specification does not establish SW4 travel.

- Integral flexure: one lid print, no loose actuator or retaining ring. PLA fatigue,
  spring force and print orientation need testing. PETG is a candidate if the PLA
  sample is brittle or too stiff; this is not a validated lifetime claim.
- Separate PETG/TPU actuator insert captured by broad shoulders: keeps the main lid
  PLA and permits replacing a failed spring. Adds a part, but avoids depending on
  a moving print-in-place gap. TPU may soften the click and needs a travel stop.
- Print-in-place captive rigid button: feasible in principle, but sliding gaps,
  bridging and freeing the button without damage are printer-dependent. Do not
  assume it cures the current support-removal failure. Test a small captive-button
  sample before integrating it. Sacrificial release tabs, if used, must be accessible
  and produce no trapped debris.

Prefer the flexure sample over a captive moving print for the first comparison.
Keep the current SW4 uncertainty explicit: prototype A allowed 0.25 mm rest gap and
0.55 mm total plunger travel, which was a design assumption, not verified actuation.

## End-cap proposal

Begin with two push-on cups overlapping short external case lands, with rounded
closed ends. Key each to its end so a USB-A cap cannot bottom onto the USB-C plug.
Set retention by a shallow detent or compliant grip on the case. Prototype a TPU
cup/grip or PETG detent before committing to a brittle, tight PLA press fit.
Clearance must remain adequate when a soft cap is squeezed. Rigid-shell caps with
a compliant grip are a fallback if an all-TPU cap provides too little protection.

Make caps removable without tools, with an unobtrusive grip feature. Tethers or
on-device parking are optional future choices, not assumed requirements. Check cap
overlap against the new joystick guard and screw access. Do not claim waterproofing
or a dust seal from a simple protective cap.

## Low-cost test sequence

1. Print a small flexure/button coupon in PLA and, if needed, PETG; check free return,
   repeated pressing and support-free cleanup. Confirm actuation on the real switch
   when available before freezing the actuator height.
2. Test a local joystick guard/cap sample for fabric snagging, finger access and cap
   retention without transmitting side loads into the shaft.
3. Test cap grip sections, then full end caps, checking removal force and connector
   clearance independently of the PCB.
4. Integrate the selected designs into prototype B and recheck assembly, movement,
   print orientation and populated-board fit.

## Design references

These inform the material/geometry trade-offs, not a guaranteed printable fit:

- Makelab, snap fits and living hinges: rounded cantilever roots, orientation and
  test prints matter; PLA is a poor choice for sharp, repeatedly flexed hinges.
  https://help.makelab.com/help/article/snap-fit-and-living-hinge-design-for-3d-printing
- Prusa PETG material guidance:
  https://help.prusa3d.com/article/petg_2059
