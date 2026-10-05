# Fuse Vault PCB reference

**Latest: prototype E — four supported shell snaps.** [Print instructions](prototype-e/README.md)
replace D's failed thin latch with ramped alignment posts and recesses. Print the
standard snap sample first, then E base and lid. Reuse the working D end caps and
USB-C collar. Closure must hold the collar securely without help from the end caps;
physical holding force still needs testing. Intended for occasional service access.

**Previous prototype D — slimmer case; latch failed physical test.** [Print and assembly instructions](prototype-d/README.md)
cover the 12.1 mm body, tighter PCB locators, hook-and-snap closure, TPU covers
with detents, bare joystick and tactile back-button dimple. Start with the latch
test pair. The existing USB-C collar is reusable; D needs new base, lid and covers.
Physical testing is pending; earlier prototypes are preserved.

**Previous prototype C — assembly access.** [Print and assembly instructions](prototype-c/README.md)
cover the open-top PCB/collar seats, enlarged SD-card loading clearance and
externally accessible top nut. Print the C base and lid; reuse the previous
accessories. Built after the first real V2 board trial exposed insertion
obstructions. C CAD checks pass; a physical re-test is still needed.

**Previous prototype B:** [prototype-b/README.md](prototype-b/README.md) contains
print files and instructions for the flat lid, wider/lower TPU D-pad, integral
back-button tab and two TPU USB end caps. Start with the small back-button coupon.
[REVISION_B_REQUIREMENTS.md](REVISION_B_REQUIREMENTS.md) records the feedback and
current direction. B printing and populated V2 board fit remain pending.

**First case prototype now available:** [prototype-a/README.md](prototype-a/README.md)
contains the five-part PLA/TPU fit prototype, single M2 screw, hooked lid and captured
USB-C collar. Use `build_case.py` and `prototype_parameters.json` for that design.
Its corrected reference and validation are separate from the original files below.

**2026-09-21 evidence update:** See [CASE_DESIGN_BRIEF.md](CASE_DESIGN_BRIEF.md)
for the supplied screen drawing, verified USB shell heights, USB-C support
requirement, captive controls and enclosed SD card. Manufacturer PDFs are now in
`datasheets/`. The reference exports below remain revision 1: the display thickness
and USB-C mounting datum still require correction and regeneration before case CAD.

Mechanically useful **reference revision 1**, built from the supplied Gerber archive, BOM, CPL and top/bottom renders. No enclosure has been designed. The board geometry and assembly positions are source-derived; the components are simplified envelopes with explicitly provisional Z dimensions.

## Open these files

- **reference.step**: coloured, named assembly with PCB, 71 populated component envelopes and one clearly named TFT placeholder.
- **pcb.step**: exact nominal bare-board geometry, including all unique drilled holes, slots and the rectangular cutout.
- **keepouts.step**: 98 separately named clearance, terminal, solder, locating-peg and access volumes. Import in the same coordinates as reference.step.
- **reference.stl**: millimetre-scale, multi-solid preview. Components can overlap and touch; this is not a single watertight printable assembly or an enclosure.
- **pcb.stl**: bare-board mesh.
- **reference_preview.png**: top and underside views of the actual generated geometry.
- **top_validation.png / bottom_validation.png**: model envelopes and extracted features registered against the supplied renders.
- **parameters.json**: editable assumptions, with evidence beside each important component.
- **drill_schedule.csv / placements.csv / component_envelopes.csv**: extracted drill data, BOM-linked placement data, and generated component bounds.
- **validation.json**: machine-readable geometry and render-registration checks.

## Board geometry

All coordinates are **millimetres in the original EasyEDA Gerber coordinate system**, viewed from the top. X increases toward USB-A. Y is negative across the board. The bottom PCB surface is Z=0 and the top is Z=1.6. Bottom-side component coordinates are not mirrored; only the bottom-view image is mirrored for comparison.

| Feature | Nominal value / bounds | Evidence |
|---|---|---|
| Board size | 73.335 x 21.237 mm | Original GKO outline centreline |
| X extent | -0.127 to 73.208 | GKO |
| Y extent | -21.237 to 0 | GKO |
| Thickness | 1.6 mm | Gerber ZIP manufacturing metadata: `erp_parameter_file.json`, `stencil_ply=1.6` |
| Internal rectangular cutout | 2.540 x 15.748 mm | GKO |
| Cutout X | 11.42583 to 13.96583 | GKO |
| Cutout Y | -19.52613 to -3.77813 | GKO |
| Unique drill features | 105 round holes + 2 routed slots | Three original Excellon files, duplicate via records removed |
| USB-C slots | 0.50002 diameter cutter; 1.29997 centreline travel; 1.79999 overall length | PTH G85 records |

The large green areas above and below the circuitry in the renders are manufacturing rails. The processed CAM panel outline adds approximately 25 mm at each long edge. Those rails and their tooling holes are excluded from the finished PCB reference. No dedicated enclosure screw-hole pattern is established on the finished board: the large holes near USB-A and the other non-plated holes are occupied connector/switch locating features.

These are exact **design-file nominal** coordinates, not measurements of the manufactured board. Router corner radii, V-score break residue, board thickness tolerance and plating variation are not determined by the supplied data. The cutout retains the sharp nominal corners in the GKO.

## Important component locations

These are CPL placement origins, which are not always the physical centre of the body. Explicit `offset_xy` values align the simplified body to its footprint/render. Special-component sizes and offsets are expressed in board axes; generic packages use the CPL rotation.

| Ref | BOM part | Side | CPL X | CPL Y | CPL rotation |
|---|---|---|---:|---:|---:|
| J1 | SHOU HAN AM90, C404965 | Top | 69.850 | -11.049 | 90 |
| J2 | XKB U261-121N-4BS2S, C2879827 | Top / assumed mid-mount | 1.143 | -10.668 | 270 |
| CARD1 | SHOU HAN TF PUSH, C393941 | Bottom | 57.404 | -9.779 | 0 |
| U3 | SHOU HAN 0.5-8P CTSJ-H2.0 119, C5373445 | Bottom | 23.241 | -11.557 | 90 |
| SW3 | SHOU HAN 10*10*9-6P WX, C2858290 | Top | 60.325 | -14.732 | 0 |
| SW4 | LXW-TS-45X45X38-SMD, C53447035 | Top | 49.149 | -15.875 | 270 |
| SW1 | TS-1140SP-4x4x2.0H-260, C51927161 | Bottom | 6.731 | -3.302 | 180 |
| SW2 | TS-1140SP-4x4x2.0H-260, C51927161 | Bottom | 6.858 | -17.526 | 0 |

Both USB components are represented as protruding **plugs**, consistent with the supplied renders. Their simplified solid envelopes are not detailed connector shells.

## Assumptions to resolve before committing enclosure dimensions

1. **TFT:** No display part appears in either BOM or CPL. The 27.9 x 13.5 mm rectangle comes directly from top silkscreen, but is not proven to be the glass outline or active viewing area. The cyan volume is a placeholder with a 0.5 mm PCB gap and 2.5 mm thickness. No bezel opening should be derived from it yet. The flex corridor to the cutout is a provisional clearance volume, not a verified cable route.
2. **USB Z position:** J1 uses an assumed 4.5 mm body height sitting on the top PCB surface. J2 uses an assumed 2.4 mm height with its bottom 2.0 mm below the top PCB surface. These seating heights need a drawing or physical measurement. Their XY envelopes have been compared with the renders.
3. **Controls:** SW3 total height 9.0 mm, SW4 3.8 mm, and SW1/SW2 2.0 mm are inferred from exact BOM part identifiers, not verified against mechanical drawings. Joystick base/shaft shape, movement diameter and button actuators are simplified. No separate D-pad or joystick cap is supplied or modelled.
4. **microSD:** Body XY and offset are estimated from the registered bottom render, and its 2.0 mm height is provisional. The card exits toward +Y. A 20 mm withdrawal space is provided as a design allowance; actual latched/ejected card positions and push travel are not established.
5. **FPC connector:** XY uses the bottom-silkscreen envelope; 2.0 mm height is inferred from the H2.0 identifier. Latch-opening clearance remains provisional.
6. **Other parts:** Package dimensions come from footprint names, with assumed heights. Inductor heights 4.0 and 2.0 mm are inferred from series names. Solder, pins, locating pegs, special terminals and general 0.5 mm clearances are conservative editable allowances, not tolerance-certified bounds.

The manufacturer-document links were investigated, but their PDF downloads returned access/rate-limit errors. **No height is labelled drawing-verified.** Do not infer that an assumed clearance guarantees fit.

## Validation performed

- Parsed the original metric 4.5-format outline and metric Excellon files. Unsupported drill/outline syntax causes extraction to fail rather than silently dropping geometry.
- Deduplicated identical drill records across the PTH/via files; retained both original source references in the drill schedule.
- Checked that the PCB is one valid solid and every extracted drill centre passes through it.
- Independently calculated PCB volume from rectangle areas and circular/slot areas: **2383.867963245 mm3**, matching the exported STEP geometry.
- Re-imported the STEP files: **73 reference solids** and **98 keep-out solids**, all valid, with preserved volumes.
- Registered the top and mirrored bottom images using board edges, then compared seven independently selected visible component centres. Maximum discrepancy: **1.134 pixels**, approximately **0.115 mm at the render scale**. This is an image-registration result, not an assembly tolerance claim.
- Visually inspected the top/bottom overlays and depth-buffered 3D preview. The display guide, cutout, connector arrangement, joystick and bottom-side controls correspond to the supplied renders.

Z heights cannot be validated from these orthographic top/bottom renders. This revision is ready for spatial planning and measurement correction; close-fitting enclosure openings depend on resolving the assumptions above.

## Regenerate or change dimensions

Tested with Python 3.13 and CadQuery 2.8.0. From this directory:

```powershell
python -m pip install -r requirements.txt
python extract_geometry.py
python build_reference.py
python validate_and_preview.py
```

Edit `parameters.json` for body sizes, seating offsets, display dimensions and access allowances. Set `display.enabled` to false to remove the TFT placeholder. The extraction script preserves the original source files and creates `geometry.json`. To change source placements, update the source CPL and run extraction again.

`reference.step` and `keepouts.step` have the same origin and units. Keep the assemblies separate in CAD. The keep-outs intentionally overlap one another and the reference; they are spaces reserved from a future case, not extra physical parts. The PCB keep-out is a conservative bounding box and reserves the internal cutout too.

## Source provenance

The original archive was `fuse-vault_PCB1_20260916_204032.zip_Y4.zip`; geometry comes from its `12416276a_y4/yg` originals, not its translated panel CAM output. Selected source Gerbers and drills are included. The supplied top and bottom PNGs are preserved as `source/top.png` and `source/bottom.png`.

The two user-supplied files are tab-delimited UTF-16 despite their `.csv` extension:

- `d195f926bac54f4a8a605b863e6e7800_1789587642000.csv` -> `source/assembly-a.csv` (BOM).
- `d195f926bac54f4a8a605b863e6e7800_1789587642000 (1).csv` -> `source/assembly-b.csv` (CPL).

`source_hashes.json` records SHA-256 digests for the included source files. Factory account/order metadata and signed download links are not redistributed.

Manufacturer-document lookup locations, for resolving the remaining assumptions:

- [AM90](https://jlcpcb.com/partdetail/SHOUHAN-AM90/C404965)
- [U261-121N-4BS2S](https://jlcpcb.com/partdetail/XKBConnection-U261_121N4BS2S/C2879827)
- [TF PUSH](https://jlcpcb.com/partdetail/SHOUHAN-TFPUSH/C393941)
- [10*10*9-6P WX](https://jlcpcb.com/partdetail/SHOUHAN-10_10_9_6PWX/C2858290)
