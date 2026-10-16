# Fuse Vault G — panelled lid

> **Archived experiment — not the current working version.** Use [prototype F](../../prototype-f/README.md) as the baseline.

Cosmetic alternative to G, based on the supplied panelled reference image. The
case keeps G's 11.1 mm body height, lowered controls, flush reversible caps,
0.35 mm shell snap engagement and optional screen shims. Existing G base, caps,
controls, collar and shims can be reused. Only the lid needs a new print; reprint
the D-pad dark and select button orange if changing their existing colours.

## Appearance and geometry

- Light-grey face panels with a charcoal structural lid and narrow dark rim.
- Stepped diagonal joint separates the Vault logo panel from the main screen and
  controls panel. A shorter diagonal defines the small corner panel.
- Panel seams are nominally 0.8 mm wide and recessed **0.2 mm**. Panel faces stay
  at the G top height; the case does not grow taller.
- The Vault arms are dark, with an orange centre. One small orange seam accent
  replaces the previous long orange circuit traces.
- D-pad and back button are dark; select button is orange. These are separately
  printed controls, with exactly the same geometry as G.

These are colour regions of one printed structural lid, not detachable panels.
The seams avoid the four snap roofs. Nothing below Z=6.4 mm changes; the normal
roof retains approximately 1.05 mm thickness under a 0.2 mm recessed seam.
The appearance is modelled in CAD; the PNGs are actual geometry renders with an
illustrative screen envelope. Tiny reference lettering and metal finishes have
not been copied.

## Files and printing

**Start with `lid-ams.3mf`.** This is a standard multipart model, not a pre-sliced
printer project. Assign the parts to your AMS filaments:

| Part | Colour |
|---|---|
| lid | Charcoal |
| lid_inlay_grey | Light grey |
| lid_inlay_orange | Orange |

Use compatible filaments from the same material family. The lid is oriented face
down. Colour regions occupy the first 0.4 mm. The shallow seam floors start
0.2 mm above the bed and bridge the narrow gaps between adjacent face regions;
inspect the first layers in your slicer. Do not fill these cosmetic recesses with
supports. A smooth build plate will emphasise the clean panel faces; the first
physical print still determines surface quality and seam definition.

Alternative formats:

- `lid-ams.step`: named, print-oriented colour assembly.
- `lid.stl`, `lid_inlay_grey.stl`, `lid_inlay_orange.stl`: import together as **one
  multipart object** and keep their shared placement. Do not arrange separately.
- `lid_single_colour.stl`: complete lid with recessed panel seams but no colour
  separation, useful for a quick fit print.
- `case-assembly.step`: assembled geometry for inspection, not a print layout.

The normal `lid.stl` has colour pockets; print it with both inlays. The colour
parts are not loose inserts to print and glue afterward.

Other included STL/STEP parts are exact copies of G. For initial fit testing,
print the included unchanged G snap/cap coupons before committing
to a full multicolour print. For full assembly and screen dry-fitting guidance,
see [G assembly instructions](baseline-G-instructions.md). The three supplied shim heights
are alternatives, not a stack; start at 1.6 mm and leave clearance under the bezel.

## Verification

`validation.json` records solid validity, watertight exported meshes, non-overlap
of colour bodies, unchanged snap roofs, and exact geometry preservation below
the colour layer. The assembled lid is the G lid with only the shallow seams
removed: it adds no material that could reduce G's internal clearances.
`closure-validation.json` rechecks the exported lid's sampled closing path and
positive retention at all four snaps. Mechanical force, tolerances and printing
appearance still need a physical test.

Rebuild in the repository with CadQuery 2.8 and trimesh:

```
python CaseDesign/experiments/build_panelled_g.py
python CaseDesign/experiments/check_panelled_g_closure.py
python CaseDesign/experiments/render_panelled_g.py
python CaseDesign/experiments/package_panelled_g.py
```
