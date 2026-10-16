# Fuse Vault H — simple recessed panels

> **Archived experiment — not the current working version.** Use [prototype F](../../prototype-f/README.md) as the baseline.

H simplifies G's panelled styling into shallow geometry on a single-piece lid.
No colour bodies, loose faceplates, inserts or panel glue are required. The G
11.1 mm body, PCB seating, firmer shell snaps and screen-shim options are retained.
The D-pad and round buttons are smaller and must be printed with the H lid.

## Print this first

Use `PRINT/lid.stl` in the downloaded ZIP (or `lid.stl` in this directory).
It is one complete, watertight solid, oriented face down. **Do not import the
assembled case STEP as a printable object.** Reference assemblies include gaps
and separately installed components, as they should.

For the two-colour face:

1. Use 0.20 mm first and subsequent layers for the first 0.4 mm.
2. Print the first two layers light grey.
3. Change to charcoal for layer 3 onwards, starting at a printed height of
   0.40 mm. In a slicer whose layer label gives the layer's top height, this is
   the layer ending at 0.60 mm. Check the colour preview before printing.

The valleys and Vault emblem are 0.4 mm deep. Their floors begin with the first
charcoal layer. This makes the face light grey, with dark recessed lines and a
dark logo, using a single colour change. The logo centre is also dark in this
simple scheme. Orange is reserved for the separately printed select button.
There is no need to paint facets or manage separate inlay meshes.

The recessed floors require small bridges: approximately 0.8 mm across the
panel lines and a few millimetres across the logo. Inspect these first charcoal
paths in the slicer. This is much smaller than bridging an entire panel; it has
not yet been verified with a physical print. Do not put support in the cosmetic
valleys. A monochrome lid uses exactly the same STL and retains all the geometry.

## Controls and assembly

Print `dpad.stl` and `back.stl` dark, and `select.stl` orange, in the proven control
material from your earlier fit tests. The three controls are face-down on the bed.
Use all three **H controls** with the H lid; G/F controls are not interchangeable
with this revised aperture and guide layout.

The D-pad face spans 14.8 mm, down from 16 mm, and the round button face diameter
is 3.7 mm. Their PCB switch centres, actuator-tip heights and central D-pad pivot
are preserved. The minimum plastic web at the face is 1.44 mm beside Back and
1.66 mm beside Select. The hidden skirt is locally relieved around the guides;
the visible openings remain separate. Check return, rocking, retention and feel
on the real switches before relying on the enclosure.

The G base and USB-C collar can be reused. Four shell snaps retain G's 0.35 mm
engagement. Install the controls inside the lid, engage the existing USB-C-end
hooks, and close the lid without pressing on the display. The original G snap
and cap fit coupons are included in `SAMPLES/` for material/fit checks.

## Matching caps, with no inserts

The caps now have shallow valleys on both broad faces. Print them **closed-end
down**, as oriented in their STLs, in the working TPU. Light grey makes the caps
visually match the lid panels; they do not need to be multicolour. The lid's
face-down colour-change recipe does not apply to the cap orientation.

Both caps remain reversible, with G's internal seating and detents unchanged.
The valleys reduce the local nominal cap roof thickness from 1.0 to 0.6 mm; the
side walls and attachment features are preserved. Check the thin decorative
regions in the slicer and test their durability in your TPU. Old G caps remain
compatible if you prefer them. USB-A and USB-C covers are different lengths.
They print separately and slide onto the device; they are not printed attached.

## Screen

G's 1.6, 1.8 and 2.0 mm shim options are included in `SHIMS/`. Use only one.
Start with 1.6 mm and dry-fit before gluing. Allow for adhesive, display thickness
and flex slack, and leave clearance beneath the bezel. A 1.8 mm shim with the
nominal 1.5 mm module puts the display face at Z=4.9 mm before adhesive, versus
Z=5.55 mm at the lid underside. Never use the shell to clamp the screen.

## Pack organisation

- `PRINT/`: complete printable parts, already oriented. One solid per STL.
- `SHIMS/`: alternative screen heights.
- `SAMPLES/`: unchanged G snap/cap test pieces.
- `CAD/`: assembly and individual STEP references; not print layouts.
- `PREVIEWS/`: CAD renders. Lid colours show the proposed layer change; caps are
  illustrated in light grey TPU, controls in dark/orange.
- `CHECKS/`: clearance, closure and mesh validation records.
- `SOURCE/`: build scripts, to run from the original repository with G inputs.

## Validation

The build checks solid validity, manifold STL meshes, measured face webs,
assembled clearances against current PCB models, ±6° D-pad tilt in four axes,
0.6 mm button travel, the raised-screen reserve, cap symmetry and connector
clearance during cap removal. Separate checks cover the closing path and snap
retention. Actual slicer settings, extrusion behaviour, material strength and
physical fit still require a print test. Earlier G variants are preserved.

Rebuild with CadQuery 2.8 and trimesh:

```
python CaseDesign/experiments/build_case_h.py
python CaseDesign/experiments/check_case_h_closure.py
python CaseDesign/experiments/render_case_h.py
python CaseDesign/experiments/package_case_h.py
```
