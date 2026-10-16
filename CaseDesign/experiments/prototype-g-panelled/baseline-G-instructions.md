# Fuse Vault — prototype G

> **Archived experiment — not the current working version.** Use [prototype F](../../prototype-f/README.md) as the baseline.

Fit-test revision: lower shell and controls, flush reversible TPU covers, firmer
shell snaps, a three-colour Vault lid, and selectable screen shims. Earlier
prototypes are preserved. CAD validation is not a physical print/fit test.

## Changes from F

| Feature | F | G |
|---|---:|---:|
| Shell height | 12.1 mm | 11.1 mm |
| Overall height including D-pad, covers off | 13.25 mm | 12.25 mm |
| End-cap section | 29.1 × 15 mm | 26.2 × 11.1 mm |
| Snap engagement | 0.25 mm | 0.35 mm |
| Screen mounting | Glued to PCB | Optional 1.6 / 1.8 / 2.0 mm shim |

The roof is 1 mm lower. The control skirts are lowered 0.4 mm and their thumb
surfaces trimmed; the switch-contact tips and the D-pad's PCB-resting pivot stay
at the F heights. Hidden guide reliefs allow the lowered controls to move.
Use the new controls with the G lid; do not substitute F controls.

The TPU caps slide over recessed shoulders and stop at a nominal 0.15 mm axial
seam. Their outside matches the nominal body section. Side detents are symmetric
about the cap centre, so each cover can be fitted either way up. USB-A and USB-C
covers remain different lengths and are **not interchangeable**. Do not use the
old caps. The cap wall is 1 mm, with a 1.3 mm closed end. Neck roofs are interrupted
at the connector openings; the USB-C collar remains a separate captured part.

## Print first

1. Print `base_snap_coupon.stl` and `lid_snap_coupon.stl` in the intended rigid
   shell material. Check that they seat fully, latch more firmly, and reopen
   without whitening or cracking. These retain both opposing shell walls.
2. Print `base_cap_coupon.stl`, `lid_cap_coupon.stl`, and `usb_c_end_cap.stl`.
   The small shell sections are held together by hand for this cap test; they
   do not include the full shell's snap closure. Test the cap both ways up.
3. Print the full base, lid and three controls only after those fits are useful.
   Both ends still require final checks with the real connectors installed.
4. Dry-fit the screen shim with the actual display and adhesive allowance before
   gluing anything. Start with 1.6 mm and increase only if there is clearance.

STLs are oriented for printing: base on its underside, lid/controls face down,
caps on their closed ends, and shims flat. Use the proven shell and control
materials from the earlier fit tests. Print caps separately in the existing TPU;
this pack does not assume that TPU can run through your AMS. A 0.2 mm layer height
is a starting point; inspect the slicer preview, especially guides and thin caps.

## AMS lid

The Vault emblem is redrawn as printable polygons from the supplied raster brand
sheet, with two grey arms and an orange centre. Orange 45-degree traces echo the
emblem. Inlays are 0.4 mm deep and flush with the lid surface.

- `lid-ams.3mf` is a standard three-part model with colour metadata and shared
  alignment. Assign the three parts to your AMS slots and select your own printer
  and filament settings; it is not a pre-sliced printer project.
- `lid-ams.step` is a named multipart lid, already face down, with three colour
  groups. Assign charcoal, pale grey and orange to compatible filaments of the
  same material family. Verify that the slicer retains the groups as parts of
  one object.
- Alternatively import `lid.stl`, `lid_inlay_grey.stl` and
  `lid_inlay_orange.stl` together as **one multipart object**. They share an origin;
  do not centre or arrange the three files independently.
- Use `lid_single_colour.stl` if printing without inlays. The normal `lid.stl`
  contains the recesses and is not the undecorated alternative.

The three colours occupy the first 0.4 mm of the face-down print; the remainder
is shell material. STEP colours are previews, not an AMS spool configuration.
No printer-specific sliced project or filament profile is included.

## Screen shim selection

The supplied display drawing specifies a 1.5 ±0.1 mm module. The older F build
still used an obsolete 2.5 mm placeholder. G uses the drawing thickness for this
fit study. The user's roughly 4 mm current recess is an estimate, not a measured
mounting datum.

Each shim is a broad 25.4 × 12.5 mm support under the display backlight, leaving
the negative-X flex exit free. It rests on the modelled clear PCB region and
supports the module's back, not the glass face. The three files are alternatives,
**not layers to stack**. Fix to the PCB and display backing with thin adhesive;
allow for both glue layers and avoid any metal exposed on the module underside.

Nominal height calculation, using the 1.8 mm shim:

- PCB top: Z = 1.6 mm.
- Shim top: Z = 3.4 mm.
- Display face: Z = 4.9 mm before glue.
- Lid underside over the module: approximately Z = 5.55 mm.
- Lid outer face: Z = 6.8 mm.

Thus the 1.8 mm shim leaves about 0.65 mm under-bezel clearance before adhesive
and module tolerance; the visible recess is about 1.9 mm before adhesive. The
2.0 mm option reduces both by another 0.2 mm. The larger clearance reserve in CAD
includes a 1.6 mm module and 0.2 mm total adhesive on the tallest shim.

Choose the tallest option that still leaves a clear gap beneath the bezel when
the case snaps fully shut. Never use the lid to compress the display or force the
flex into place. Confirm cable slack and the active-area alignment on the actual
screen before bonding; the flex path is not validated by its flat length alone.
The remaining bezel depth is intentional: making the glass flush would require
a different bezel/display mounting arrangement.

## Validation and source

`validation.json` records solid/mesh checks, assembled clearances against the
current board, screen reserve, D-pad tilt, button travel, cap symmetry and
removal, SD access and collar capture. `closure-validation.json` records sampled
hooked closure and positive snap engagement. The cap ribs and shell beads must
flex during operation; geometric overlap at these features is not a force or
fatigue prediction.

`case-assembly.step` uses assembly coordinates. Individual STEP files use those
same coordinates except `lid-ams.step`, which is specifically print-oriented.
The PNGs are renders of the exported CAD with an illustrative raised display
envelope; the display is not a printable part. Rebuild using CadQuery 2.8 and trimesh:

```
python CaseDesign/experiments/build_case_g.py
python CaseDesign/experiments/check_case_g_closure.py
python CaseDesign/experiments/render_case_g.py
python CaseDesign/experiments/package_case_g.py
```
