# Prototype E — four ramped shell snaps

Replaces the failed thin D latch with short, broad alignment posts and matching
sloped recesses. Intended for a firm closure and occasional opening, including
holding the USB-C collar captive with the USB end covers removed. **Physical
holding strength remains untested.** No thin spring arm or sacrificial support web.

## Print tonight

1. Start with **base_snap_standard.stl + lid_snap_standard.stl**. These use the
   same 0.25 mm engagement as the supplied full case. They are short cross-sections
   with two opposing walls, so they exercise more than an isolated flexible tooth.
2. Use your **0.4 mm nozzle**, 0.2 mm layers and the shell material/settings you
   intend for the case. Inspect the slicer preview: stems, ramps and recesses must
   have continuous extrusion paths. Keep supports off the snap mating surfaces.
   All STLs are already bed-oriented: base floor down, lid outer face down.
3. Let the sample cool, clear stray strings, align it and press the two sides home.
   It should seat fully and resist being pulled apart. There is no web to cut.
   A sample that barely catches or falls apart is a failed fit, not ready for USB-C
   retention. A sample that needs excessive force should not be forced onto a board.
4. Print **base.stl + lid.stl** after the standard sample works. Reuse the working
   D USB covers and existing USB-C collar. Do not mix E and D shell halves.

Additional sample pairs: `*_snap_light.stl` use 0.15 mm engagement;
`*_snap_firm.stl` use 0.35 mm. They are comparison samples only. The supplied full
shell is **standard**, not firm. If only another sample works, the full shell
needs regenerating with that engagement before printing. Local samples do not
establish the complete case's stiffness or retention force.

## What changed

- Four supported snap posts: one on each side near USB-C and another pair near
  USB-A. Front posts are 3.5 mm wide and rear posts 3 mm wide, with 1 mm stems,
  wider roots and sloping catches. Surrounding shell walls provide the small
  elastic movement, rather than a separately slotted spring arm.
- Nominal 0.25 mm engagement beyond the entry clearance. The ramps/recesses slope
  in both printing directions. These are detents, not irreversible barbs.
- Original USB-C-end hooks remain. The old D latch cuts, catch and support web
  are removed; that side wall is solid again.
- Board supports are independent of the snaps. Populated-board loading, the
  larger SD notch, 12.1 mm body thickness, controls and flex pocket are unchanged.
- D end-cap shapes and USB-C collar are unchanged. `preserved_geometry.json`
  also checks that the case's USB-C capture region matches D.

## Assembly and retention check

Fit the SD card and USB-C collar to the board first, then lower them together
into the base. Arrange the cable without pinching it. Engage the original
USB-C-end lid hooks, lower the lid, then press beside each opposing snap pair.
Press on the rim, not on the screen, joystick or back button. Check that all four
positions seat and that the seam closes evenly.

First try the empty case without end covers. Gently pull the halves apart near
both ends: the four snaps must hold on their own. Then install the board and
check that ordinary USB insertion/removal does not open the seam or displace the
collar. Do not use the bare connector as a lever or a destructive strength test.
If a snap does not hold, stop and adjust the engagement rather than relying on
an end cover to provide the missing USB-C retention.

Opening is deliberately less convenient than closing. Remove the USB covers,
work a thin plastic pick along the side seam near a snap, and release one region
at a time. Avoid levering on the connectors or pushing tools into the electronics.
The design is for a few service openings, not frequent daily access. Inspect for
cracks after opening; infrequent use does not make a damaged snap safe to reuse.

## Files and checks

`base.stl` and `lid.stl` are the only new full-size prints needed. Accessory STLs
are included for completeness. STEP parts retain assembly coordinates; assembly
STEPs include simplified electronics and are not single printable objects.

`validation.json` covers solids, STEP round trips, closed bed-oriented meshes,
static/sample fit, board/SD/collar insertion, bare-shaft clearance, lid closure,
end-cover fit and axial collar capture. During closure, intentional interference
is permitted only on the outward snap beads; the stems and other case geometry
must remain clear. A 0.4 mm sample separation encounters the retaining ramps.

These checks do **not** establish wall stress, pull-off force, print tolerance or
USB-C load capacity. Test the real printed parts with the end covers removed.
The slimmer D cable allowance and other provisional electronics dimensions are
unchanged. Earlier A/B/C/D files and ZIPs are preserved.

Rebuild using CadQuery 2.8 and VTK:

```sh
python CaseDesign/build_case_e.py
python CaseDesign/render_case_e.py
```

`P['snap_engagement']` sets the full-case engagement. The three sample pairs are
fixed at 0.15 / 0.25 / 0.35 mm. Repackage the ZIP after rebuilding.
