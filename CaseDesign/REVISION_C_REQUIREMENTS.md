# V2 prototype C: assembly access

Feedback: the first real V2 boards are now available. Overall fit is close, but
installation is obstructed by the PCB groove, USB-C bridge and small SD recess.
The user requests an accessible nut on top with the bolt passing through below.
No clarification was needed for the text-to-speech description.

Requirements implemented in prototype C:

1. Preserve the board seating height and existing controls while making the PCB
   seat open from above, with larger lateral clearance and an entry lead-in.
2. Allow collar and populated PCB to enter together. Preserve the fit-tested
   collar bore and retain its flange once the lid is closed.
3. Install the SD card before inserting the board. Enlarge both its seated
   clearance and entry path; no requirement for external SD removal in use.
4. Provide an open top nut recess and through screw, so no nut must be held or
   trapped inside during assembly. Use nominal existing M2 hardware where possible.
5. Check assembly paths as well as final fit. Preserve A/B files for comparison.

Implementation and limitations are documented in `prototype-c/README.md`.
The body extends 1.2 mm at USB-A to retain an approximately 1 mm end wall around
the enlarged PCB opening. The existing USB-A cover seats farther out unchanged.
Assembly CAD validation includes continuous conservative PCB, collar and SD
sweeps; full populated board and hardware paths are also sampled.

A real-board trial of the old case does not establish the new case's fit,
screen/flex route, card protrusion, back-button actuation or print tolerances.
