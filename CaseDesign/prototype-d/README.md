# V2 case — prototype D

Redesigned from physical feedback: board movement in C, weak end-cap retention,
preference for a latch instead of a screw, a slimmer enclosure and a bare joystick.
The previous back-button tab worked; it now has a tactile locating dimple.
**D still needs a physical print test.** A/B/C outputs and ZIPs are preserved.

## Main changes

| Feature | Prototype C | Prototype D |
|---|---|---|
| Body thickness, excluding joystick/covers | 13.8 mm | **12.1 mm** |
| Maximum body width | 30.6 mm | **27.6 mm**; 26.2 mm through most of the body |
| Body length | 77.3 mm | 77.3 mm |
| Overall height to bare joystick tip, covers off | 16.2 mm | **14.9 mm** |
| Board lateral allowance | 0.6 mm per side | 0.15 mm at short side locators; 0.2 mm at end locators |
| Closure | Hooks + M2 screw/nut | Hooks + side-release snap near USB-A |
| USB covers | Light friction ribs | Rounded TPU ribs engaging matching case recesses |

Board seating height and component positions are unchanged. The floor moves up
1.3 mm, and the lid surface moves down 0.4 mm. Floor thickness is 1.4 mm, locally
1.3 mm under the display-flex pocket. The normal lid roof is 1.2 mm thick.

The enlarged C SD-loading pocket is preserved, including a small local flare of
the case wall so it remains enclosed. The former screw shoulder is removed.
Locators have short sloping entries; the PCB still lowers from above, with the
card fitted and USB-C collar installed. Nominal total side play is 0.3 mm and
end play is 0.4 mm before print/board tolerances. It is not a forced press fit.

**No joystick cap:** use the existing bare square plastic shaft. The unchanged
shaft opening allows the previous assumed 1 mm radial movement. The shaft now
stands 2.8 mm above the lid; check real movement and comfort.

## Cable clearance

The provisional flex allowance is reduced from 3.5 to **3.0 mm below the PCB**,
based on the user's observation that its bulge can settle lower. A local recess
provides this depth without thinning the entire base. This is an assumed routing
envelope, not a verified bend-radius or clamping allowance. Arrange a gentle loop;
do not crease the flex or use the latch to force the case closed against it.
If it does not settle freely, retain C while the pocket is adjusted.

## Latch and assembly

The latch is on the long side near USB-A, opposite the SD pocket. A roughly
10 mm long wall spring has a 0.9 mm thickness. The lid catch has a ramp that
spreads this spring outward by a nominal 0.45 mm, then catches under its lip.
There is no screw or nut. The original USB-C-end hooks are retained.

A small **sacrificial support web** props up the spring's free end for printing.
It sits in the lower horizontal slot, close to the vertical release slit. Its
section is 0.4 x 0.4 mm. Carefully cut its middle out of the slot with a fine blade
or suitable cutter before flexing the latch; hold the spring steady. Do not force
the latch until this web is cleared. The same web is present on the coupon.

1. **Print `base_latch_coupon.stl` and `lid_latch_coupon.stl` first**, in the shell
   material you intend to use (PLA or PETG). Clear the support web, then hold the samples parallel and test
   closure, retention and release over several cycles. These cropped samples do
   not include the opposite-end hooks; twisting them is not a full-case test.
2. Print the full D base/lid only after the latch sample works. Remove old USB
   covers and the joystick cap. Fit/latch the SD card before installing the board.
3. Fit the original USB-C collar to the connector on the bench. Lower the board,
   installed card and collar into the base together. Check that it reaches all
   support pads. The shorter locators should prevent the old sliding movement.
4. Arrange the display/flex. Engage the USB-C-end lid hooks and lower the lid.
   Press near the USB-A latch until it catches; do not press on the joystick or
   force the lid down against the board or cable.
5. To open, remove the USB-A cover. Use a fingernail at the side latch window to
   **ease the base's spring wall outward**, while lifting that end of the lid.
   About 0.5 mm of outward tip motion should clear the catch. Do not bend it far
   outward or lever on the USB connector. Test access on the coupon first.

The new latch's feel and fatigue life are unknown. If the coupon is fused, brittle
or excessively tight, revise its gaps/material before printing the full case.
The electronics must not carry latch-closing loads.

## Back button

The leaf remains 0.8 mm thick with the same slot footprint, a nominal 0.25 mm rest
gap above the switch and 0.55 mm clearance to its stops. Its surface moves down
0.4 mm with the roof, the actuator stem is shortened to keep the original tip
height, and the stop supports are rebuilt accordingly. A 2.7 mm diameter,
0.25 mm deep dimple marks where to press and preserves a flat printing face.
The root transition changes with the thinner roof: check the previously successful
click and return again. There is no separate plunger or joystick cap to print.

## End covers

**Print new D covers in TPU.** Old covers do not match the slimmer case.
Each has two rounded internal ribs, located low on the case sides. The ribs
flex over the case walls by up to 0.4 mm and settle into 0.5 mm deep recesses.
This adds resistance to pulling off instead of relying on the previous light
friction fit. The ribs have rounded ends for insertion/removal.

The cups stop against the case, with nominal plug-tip clearance of 1.5 mm. They
neither grip the electrical contacts nor use the long USB-C plug as the retention
spring. The original USB-C support collar is unchanged. Check retention on the
empty case first, then on the installed board. TPU hardness and extrusion accuracy
will affect the fit; these dimensions have not been physically calibrated.
Remove covers by gripping the case and pulling the cover along the USB axis.

## Printing and files

All STL files are in millimetres and bed-oriented:

- `base.stl`, `base_latch_coupon.stl`: floor down. Inspect the latch window's short
  bridge. The built-in sacrificial web supports its free end; remove that web
  after printing and keep slicer-generated supports out of the release slots. Use elephant-foot
  compensation if your first layers close fine gaps.
- `lid.stl`, `lid_latch_coupon.stl`: outside face down. The recessed button marker
  leaves the main face flat. Inspect the short bridge over that dimple. Block
  supports in the back-button movement gaps; existing hooks may need the local
  support treatment used successfully on prior lids.
- `usb_c_end_cap.stl`, `usb_a_end_cap.stl`: TPU, closed end down and mouth up.
  The seats and rounded detents are designed for this direction. Avoid supports
  inside the cups. Test the fit of one cover before committing to both.
- `usb_c_collar.stl`: **unchanged; reuse your successful collar**.

D base, lid and covers form a new set. Do not mix C/D shells. The top-level STEP
parts retain assembly coordinates. `case_with_board.step` contains simplified
reference electronics; assemblies are not single printable objects.

## What was checked

`validation.json` records valid single-solid parts, closed/manifold bed-oriented
STLs, STEP round trips, final clearances, populated-board entry, continuous
conservative PCB/collar/SD loading sweeps, sampled lid closure, shaft movement and
end-cover removal. Closure permits geometric interference **only in the designed
latch spring**; cap removal similarly records intended TPU rib interference.
These are elastic-fit design allowances, not stress/force simulations.

Actual board tolerances, the revised cable route, latch force/life and cap pull-off
force remain unverified. The back-tab change also needs a quick functional check.
Use the physical trial to set the next clearance adjustment rather than forcing
parts to fit. This is a prototype, not a seal or waterproof enclosure.

## Rebuild

With `CaseDesign/requirements.txt` installed, from the repository:

```sh
python CaseDesign/build_case_d.py
python CaseDesign/render_case_d.py
```

D uses the original reference generator and reads the successful C collar STEP.
Dimensions are in `P` and the feature definitions in `build_case_d.py`. Outputs
are isolated in `prototype-d/`. Repackage the ZIP after rebuilding.
