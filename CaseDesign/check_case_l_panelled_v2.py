"""Independent exported-geometry checks for the softened L panelled variant.

This checker reads STEP files afresh. It does not import the variant builder or
use its geometry helpers. Source-electronics checks are limited to the added
exterior material: preserved L volumes inherit L's existing evidence.
"""
from pathlib import Path
import hashlib
import itertools
import json
import cadquery as cq
import trimesh

ROOT = Path(__file__).resolve().parent
BASE = ROOT / 'prototype-l'
OUT = ROOT / 'prototype-l-panelled-v2'
TOL = 1e-4


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane('XY').box(x1-x0, y1-y0, z1-z0, centered=False).translate((x0, y0, z0)).val()


def volume(shape):
    return max(0., shape.Volume()) if shape.Solids() else 0.


def overlap(a, b):
    if not a.Solids() or not b.Solids():
        return 0.
    aa, bb = a.BoundingBox(), b.BoundingBox()
    if (aa.xmax <= bb.xmin+1e-7 or bb.xmax <= aa.xmin+1e-7 or
        aa.ymax <= bb.ymin+1e-7 or bb.ymax <= aa.ymin+1e-7 or
        aa.zmax <= bb.zmin+1e-7 or bb.zmax <= aa.zmin+1e-7):
        return 0.
    return volume(a.intersect(b))


def outside_volume(shape, mask):
    return volume(shape.cut(mask)) if shape.Solids() else 0.


def difference(a, b):
    return volume(a.cut(b)) + volume(b.cut(a))


def load(folder, name):
    return cq.importers.importStep(str(folder / (name+'.step'))).val()


def run():
    report = json.loads((OUT/'validation.json').read_text())
    names = ['base', 'lid', 'dpad', 'select', 'back', 'screen_shim',
             'usb_c_collar', 'usb_c_end_cap', 'usb_a_end_cap']
    old = {n: load(BASE, n) for n in names}
    shapes = {n: load(OUT, n) for n in names}
    color_names = [n for n in report['colors'] if n.startswith('lid') and n != 'lid']
    colors = {n: load(OUT, n) for n in color_names}
    lid = shapes['lid'].fuse(*colors.values()).clean()
    shapes['lid'] = lid
    checks, failures = [], []

    def check(label, ok, measured=None):
        row = {'check': label, 'passed': bool(ok)}
        if measured is not None:
            row['measured'] = measured
        checks.append(row)
        if not ok:
            failures.append(row)

    def clear(label, a, b):
        v = overlap(a, b)
        check(label, v < TOL, v)

    for n in ['validation.json', 'detail-validation.json']:
        check('L baseline evidence passes '+n, not json.loads((BASE/n).read_text())['failures'])
    for n, h in report.get('source_sha256', {}).items():
        check('L input unchanged '+n, hashlib.sha256((BASE/n).read_bytes()).hexdigest() == h)
    for n in names:
        check(n+' valid connected assembly solid', shapes[n].isValid() and len(shapes[n].Solids()) == 1)
    for n in names[2:]:
        check(n+' exactly preserves L', difference(shapes[n], old[n]) < TOL)
    check('Structural coloured lid connected', load(OUT, 'lid').isValid() and len(load(OUT, 'lid').Solids()) == 1)
    for (an, a), (bn, b) in itertools.combinations({'lid': load(OUT, 'lid'), **colors}.items(), 2):
        clear('Disjoint colour bodies '+an+' / '+bn, a, b)
    check('Single-colour lid matches complete multipart lid', difference(lid, load(OUT, 'lid_single_colour')) < TOL)

    # Explicit scope of permitted mechanical envelope changes, independent of
    # the builder's spline construction. Existing shoulder itself remains intact.
    apron_scope = box(40, 50.7, 1.9, 4.05, -4.10001, 7.25001)
    decorative_layer = box(-3, 75, -24, 4.1, 6.88999, 7.25001)
    added = {}
    for n in ['base', 'lid']:
        added[n] = shapes[n].cut(old[n])
        removed = old[n].cut(shapes[n])
        check(n+' new material confined to exterior shoulder blend', outside_volume(added[n], apron_scope) < TOL)
        allowed_removal = apron_scope if n == 'base' else apron_scope.fuse(decorative_layer)
        check(n+' removals confined to exterior/cosmetic layer', outside_volume(removed, allowed_removal) < TOL)
        # The entire SD opening, upper perimeter and loaded card remain unchanged.
        protected = box(50.7, 68.1, -16, 2.50001, -4.2, 6.88999)
        check(n+' SD-side mechanics unchanged', overlap(added[n], protected)+overlap(removed, protected) < TOL)
        clear(n+' installed SD reserve', shapes[n], box(50.7, 68.1, -15, 2.5, -2.7, 0))
    clear('PCB vertical loading reserve', shapes['base'], box(-.127, 73.208, -21.237, 0, .001, 16))
    clear('SD vertical loading reserve', shapes['base'], box(50.7, 68.1, -15, 2.5, -2.7, 16))
    screen = box(16.95146, 45.15146, -18.64208, -4.84208, 4.00001, 5.8)
    for n in ['base', 'lid']:
        clear(n+' raised display reserve', shapes[n], screen)

    # The changes must not alter any of L's four snap posts, roofs or pockets.
    flex_regions = []
    for x0, x1 in [(6.05, 10.45), (69.55, 73.45)]:
        for y0, y1 in [(0, 2.7), (-23.9, -21.2)]:
            region = box(x0, x1, y0, y1, 3.4, 6.89)
            for n in ['base', 'lid']:
                check(n+' snap volume preserved '+str((x0, y0)),
                      difference(old[n].intersect(region), shapes[n].intersect(region)) < TOL)
    # Only existing positive bead tips are excluded during shell closure.
    bead_volumes = []
    for x0, x1 in [(6.5, 10), (70, 73)]:
        bead = cq.Workplane('YZ', origin=(x0, 0, 0)).polyline(
            [(1.19,4.7),(1.70,5.15),(1.70,5.35),(1.19,5.95)]
        ).close().extrude(x1-x0).val()
        for lower in [False, True]:
            region = box(x0-.01,x1+.01,1.35,3,4.4,6.1)
            b = bead
            if lower:
                b = b.mirror('XZ', (0,-10.6,0))
                region = region.mirror('XZ', (0,-10.6,0))
            bead_volumes.append(b.intersect(region))
    fixed_base = shapes['base'].cut(cq.Compound.makeCompound(bead_volumes))
    for angle in [0, .5, 1, 2, 5, 10, 15]:
        moved = lid.rotate((-.5,0,3.2), (-.5,1,3.2), -angle)
        for on, obstacle in [('base excluding snap beads', fixed_base),
                             ('collar', shapes['usb_c_collar']),
                             ('display', screen), ('screen shim', shapes['screen_shim'])]:
            clear('Closing lid '+str(angle)+' / '+on, moved, obstacle)
    for axis in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
        for angle in [-4,-2,0,2,4]:
            moved = shapes['dpad'].rotate((58.77,-10.38,2.55), (58.77+axis[0],-10.38+axis[1],2.55), angle)
            clear('D-pad rock '+str(axis)+' '+str(angle)+' / lid', moved, lid)
    for n in ['select', 'back']:
        for dz in [0, -.2, -.4, -.6]:
            clear(n+' stroke '+str(dz), shapes[n].translate((0,0,dz)), lid)
    for (an,a), (bn,b) in itertools.combinations(shapes.items(), 2):
        clear('Closed assembly '+an+' / '+bn, a, b)

    # Added external volumes must clear the source board, including populated
    # components. Only additions can create a new electronics collision.
    source = ROOT/'pcb-print-model/source-board.step'
    reference = cq.importers.importStep(str(source)).val()
    for n, s in added.items():
        for i, component in enumerate(reference.Solids()):
            clear(n+' added blend / source electronics '+str(i), s, component)
    for n in report['meshes']:
        mesh = trimesh.load(str(OUT/(n+'.stl')), force='mesh')
        check(n+' watertight outward STL', mesh.is_watertight and mesh.is_winding_consistent and all(m.volume>0 for m in mesh.split()))
        s = load(OUT, n)
        check(n+' STL volume agrees with STEP', abs(mesh.volume-s.Volume()) < max(.1, s.Volume()*.002))
    for n in ['lid', *color_names]:
        s = load(OUT,n).rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,7.25))
        b = s.BoundingBox(); mesh = trimesh.load(str(OUT/(n+'.stl')), force='mesh')
        check(n+' shared face-down registration', abs(mesh.bounds-[[b.xmin,b.ymin,b.zmin],[b.xmax,b.ymax,b.zmax]]).max() < .03)
    evidence = {'checks': checks, 'failures': failures,
                'basis': 'Independent STEP booleans, sampled closure and control movement, added-material checks against source electronics, STL topology and registration.',
                'limitations': ['Physical print, finish, multi-material adhesion, retention force and fatigue are not simulated.']}
    (OUT/'detail-validation.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('Panelled v2:', len(checks), 'checks;', len(failures), 'failures', flush=True)
    assert not failures, failures


if __name__ == '__main__':
    run()
