# V2 case — prototype B

Experimental print update following prototype A's successful collar, cap-socket
and closure checks. **V2 board fit and the new parts still need physical testing.**
Prototype A and its ZIP are preserved unchanged.

## What changed

- Flat lid retained: no raised joystick guard or additional support around a well.
- Round TPU D-pad cap increased from 10.4 to **15.2 mm diameter**, with rounded top
  and underside edges and four shallow direction dimples. Top reduced by 1 mm:
  **3.4 mm above the lid** instead of 4.4 mm. The successful 2.6 mm square socket,
  lead-in and seating depth are unchanged. This is a broader thumb surface, not a
  recessed or shielded joystick; test pocket snagging and leverage on the shaft.
- Back button is now a flush, integral cantilever with a small underside actuator.
  Both old rings and the separate plunger are removed. A 4 mm wide, 0.8 mm thick
  leaf runs into the thicker roof through a tapered root. Short underside ledges
  sit 0.55 mm below the leaf; nub travel at stop contact depends on bending.
  Rest gap is 0.25 mm; actual SW4
  actuation remains unverified. The joystick's datasheet stroke does not prove it.
- Two removable TPU end cups cover the USB plugs. Each overlaps the outer case,
  with shallow ribs intended to compress 0.1 mm against its side walls. Internal
  seating ramps stop against the case, not the connector. At the drawn position,
  each plug tip has 1.5 mm clearance and each seating face is 0.15 mm from the case.
  Pushing fully home consumes that 0.15 mm. Soft cups can deflect under pressure;
  connector protection and retention force must be checked physically.
- USB-C collar geometry, hook/screw closure and board supports retained. Base
  differs only around removal of the old back-button stop. **Use B base with B lid.**

With both caps fitted the nominal overall envelope is approximately
106.3 x 33.9 x 18.9 mm, including the D-pad. The body itself is unchanged in size.
The wider USB-A cap follows the existing screw shoulder rather than adding a new
screw bump. The caps are storage covers; remove them to use the USB plugs.

## Files and print sequence

STLs are in millimetres and already placed in the intended orientation on Z=0.
STEP files retain assembly coordinates. `case_with_board.step` includes simplified
reference components; those are not printable enclosure parts.

1. **back_button_coupon.stl — PLA first.** A crop of the actual lid and mechanism.
   Flat outer face down, 0.2 mm layers, no supports within the slots or stop gaps.
   Test free movement/return, stop contact and repeated pressing. Avoid elephant's
   foot fusing the 0.65 mm slots. Try PETG if PLA is too brittle or stiff. This coupon
   checks printing and feel; check switch actuation later on the populated board.
2. **joystick_cap.stl — TPU.** Flat central top/dimple face down; square socket up.
   Check the wider cap for comfortable directions and centre press, retention and
   fabric snagging. It can be tried on prototype A before printing a new shell.
3. **usb_c_end_cap.stl / usb_a_end_cap.stl — TPU.** Closed end down, open end up.
   Internal stops have ramps for this orientation. No supports inside the cups.
   Check seating and retention on the empty assembled case before fitting a board.
   Grip is a prototype interference fit: do not substitute rigid PLA unchanged.
4. **base.stl / lid.stl — PLA.** Base floor down; lid outer face down as exported.
   Use the shell print settings that worked for A. The back tab and its small stops
   are intended to print without support; block generated supports there. Existing
   hooks and other shell details may still need the local support treatment used
   for A. Inspect the preview in the slicer before printing.
5. **usb_c_collar.stl — same as A.** Reuse the already successful physical collar.
   Reuse the original screw/nut arrangement (nominal M2 x 10 and captured M2 nut).

No loose back plunger is used. Assemble the hooks and screw as before; do not force
closure against a button that is already depressed. A failed coupon means revise
its thickness/gaps before spending filament on another full lid.

## Checks and limits

`validation.json` records valid single-solid parts, STEP round trips, closed
manifold STL meshes, print-bed positioning, static reference clearances, sampled
lid closure, sampled cap movement and cap removal. Intended TPU grip interference
is reported separately from unwanted collisions. `prototype_a_preservation.json`
records the unchanged baseline hashes.

Joystick sweep uses the existing **1 mm radial XY allowance**, plus a 0.5 mm down
sample (socket gap plus centre motion allowance). This is not confirmation of the
user's earlier rough 3 mm movement estimate; check real movement before accepting
it. The lower cap leaves 0.75 mm of TPU above the socket. Retention, tearing, tab
fatigue, squeeze/crush loads, printed tolerances, display/flex installation and
populated V2 board fit are not established by CAD checks. No sealing claim.

## Rebuild

From `CaseDesign`, with the original reference sources and CadQuery 2.8 available:

```sh
python build_case_b.py
python render_case_b.py
```

B dimensions are in `P` near the top of `build_case_b.py`; the successful A geometry
is reused through `build_case.py`. Rendering uses VTK and the exported STEP parts.
The ZIP is a snapshot; regenerate it after rebuilding or changing the README.
