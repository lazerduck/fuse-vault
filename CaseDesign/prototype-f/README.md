# Prototype F — current working version

Use this version as the starting point for printing, inspection and manual CAD
changes. The G/H designs are [archived experiments](../experiments/README.md).

| File | Purpose |
|---|---|
| [case-assembly.step](case-assembly.step) | Editable enclosure assembly in assembly coordinates |
| [case-with-board.step](case-with-board.step) | Enclosure with the current board reference |
| Individual `.step` files | Edit individual parts in the same assembly coordinates |
| Individual `.stl` files | Parts already oriented for printing |
| [F print pack](fuse-vault-case-f-print-pack.zip) | Existing F print files and instructions |
| [PRINT-AND-ASSEMBLY.txt](PRINT-AND-ASSEMBLY.txt) | Print settings, part reuse, assembly and limitations |

The D-pad and buttons reuse the physically successful control-area sample.
The complete F enclosure has **not** been confirmed by a full physical print and
fit test. This current-version designation does not change that status.

The geometry, existing print pack and validation reports have not been modified.
Rebuild scripts remain one directory above:

```
python CaseDesign/build_case_f.py
python CaseDesign/check_case_f_closure.py
python CaseDesign/render_case_f.py
```
