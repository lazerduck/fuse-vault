# Archived case experiments

**The current working version is [prototype F](../prototype-f/README.md).**
Use its STEP assembly for further work. These designs are retained as experiments,
not as recommended print candidates or later releases of the working case.

| Experiment | Contents |
|---|---|
| [G](prototype-g/README.md) | Lower roof, flush caps, stronger snaps and colour inlays |
| [Panelled G](prototype-g-panelled/README.md) | Light-grey panel styling and shallow seams |
| [H](prototype-h/README.md) | Smaller controls and single-piece recessed styling |

Each directory retains its previews, validation reports and documentation in Git.
Generated CAD, STLs, 3MF files and ZIP packs remain local and are ignored; a fresh
checkout requires regeneration. The matching build, check, render and package
scripts are alongside these directories. Run them from the repository root, for example:

```
python CaseDesign/experiments/build_case_g.py
python CaseDesign/experiments/check_case_g_closure.py
python CaseDesign/experiments/render_case_g.py
```

Scripts resolve their experiment outputs here, while shared E helpers, F inputs
and PCB references remain in the parent CaseDesign directory. Rebuilding requires
the original CAD environment and source inputs.

Local original ZIPs and tracked source manifests are historical snapshots and remain unchanged;
their embedded paths, scripts or instructions may show the old locations. Use the
loose scripts and READMEs here for the relocated paths. CAD checks in these records
do not establish physical print quality, fit or retention.
