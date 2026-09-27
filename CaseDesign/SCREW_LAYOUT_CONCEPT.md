# V2 screw layout concept

2026-09-21. Planning sketch, not a printable or mechanically validated design.

Four screws can join the enclosure outside the existing PCB. Use one pair near
each end of the board, with continuous rounded side walls enclosing all four posts.
This avoids separate exterior ears and requires no board drilling or V3 revision.

All coordinates use the existing Gerber XY origin; dimensions are millimetres.

| Concept | Post envelope diameter | Screw centres | Approximate main body |
|---|---:|---|---|
| M2 | 6.5 | X=8 and 65, each at Y=3.75 and -24.987 | 80 x 36.737 |
| M3 | 8.5 | X=8 and 65, each at Y=4.75 and -25.987 | 80 x 40.737 |

The main body runs X=-3.5 to 76.5 with 5 mm plan corner radii. The USB-C cradle
extends another 1 mm at the nose. Exposed plugs are excluded from body length.
These are illustrative budgets, not minimum achievable dimensions.

Each post is 0.5 mm clear of the nominal PCB edge. Its surrounding case extends
0.75 mm beyond its circular envelope; the post itself supplies the local bulk.
The four post circles do not intersect any of the existing component-envelope XY
rectangles, checked for both concepts. This does not check cap sweep, installed SD
card, flex folds, screw access, or a complete three-dimensional assembly.

The section illustrates screws entering from underneath and engaging captive nuts
in the lid. Screw holes and nut pockets must be specified against selected hardware;
the 2.3/3.4 mm drawn clearance bores and post diameters are provisional. This is not
a heat-set insert sizing recommendation: insert body diameter and surrounding
plastic requirements depend on the selected insert. See manufacturer guidance:
https://www.spirol.com/resources/white-papers/how-to-design-the-proper-hole-for-heat-ultrasonic-inserts/

The PCB sits on separate ledges with matching upper restraints. Put these only on
confirmed clear edge regions, and stop case closure on the posts, not the PCB or
screen. The section is schematic and does not establish case height or screw length.

USB-C support remains mandatory: lower cradle plus upper saddle around the rear
shell, tied into the case. The sketch places it around X=-4.5 to 1.2, leaving about
7.26 mm of the present model's plug ahead of the support. This is a proposed fit-test
position, not a verified USB insertion requirement. Blend the nose into the case,
check against real receptacles, and use the corrected USB-C Z datum in the brief.

Recommendation: start with the M2 concept. Captive nuts permit repeated opening
without relying on printed machine threads. A V3 with dedicated mounting holes
could reduce case width, but V2 need not wait for that board revision.
