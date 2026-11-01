"""Independent exported-geometry checks for the sculpted L panel variant.

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
OUT = ROOT / 'prototype-l-sculpted'
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
    evidence_files = sorted([p for p in OUT.iterdir()
        if (p.suffix in ['.step','.stl'] and not p.name.startswith('debug_')) or p.name=='validation.json'])
    export_hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in evidence_files}
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
    for n in ['screen_shim', 'usb_c_collar']:
        check(n+' exactly preserves L', difference(shapes[n], old[n]) < TOL)
    for n, floor in [('dpad',7.79999),('select',8.09999),('back',8.09999)]:
        s, original = shapes[n], old[n]
        removed = original.cut(s)
        check(n+' only removes outer thumb-face finish',volume(s.cut(original)) < TOL)
        check(n+' contacts guides and travel geometry unchanged',outside_volume(removed,box(-30,110,-40,20,floor,9)) < TOL)
    dot_probe = cq.Workplane('XY').center(64.77,-10.38).circle(.45).extrude(.31).translate((0,0,7.7)).val()
    clear('D-pad orientation dot remains open',shapes['dpad'],dot_probe)
    check('Structural coloured lid connected', load(OUT, 'lid').isValid() and len(load(OUT, 'lid').Solids()) == 1)
    for (an, a), (bn, b) in itertools.combinations({'lid': load(OUT, 'lid'), **colors}.items(), 2):
        clear('Disjoint colour bodies '+an+' / '+bn, a, b)
    check('Single-colour lid matches complete multipart lid', difference(lid, load(OUT, 'lid_single_colour')) < TOL)

    # Explicit scope of permitted mechanical envelope changes, independent of
    # the builder's spline construction. Existing shoulder itself remains intact.
    apron_scope = box(40, 50.7, 1.9, 4.05, -4.10001, 7.25001)
    decorative_layer = box(-3, 75, -24, 4.1, 6.84999, 7.25001)
    added = {}
    for n in ['base', 'lid']:
        added[n] = shapes[n].cut(old[n])
        removed = old[n].cut(shapes[n])
        check(n+' new material confined to exterior shoulder blend', outside_volume(added[n], apron_scope) < TOL)
        allowed_removal = apron_scope if n == 'base' else apron_scope.fuse(decorative_layer)
        check(n+' removals confined to exterior/cosmetic layer', outside_volume(removed, allowed_removal) < TOL)
        # The entire SD opening, upper perimeter and loaded card remain unchanged.
        protected = box(50.7, 68.1, -16, 2.50001, -4.2, 6.84999)
        check(n+' SD-side mechanics unchanged', overlap(added[n], protected)+overlap(removed, protected) < TOL)
        clear(n+' installed SD reserve', shapes[n], box(50.7, 68.1, -15, 2.5, -2.7, 0))
    clear('PCB vertical loading reserve', shapes['base'], box(-.127, 73.208, -21.237, 0, .001, 16))
    clear('SD vertical loading reserve', shapes['base'], box(50.7, 68.1, -15, 2.5, -2.7, 16))
    screen = box(16.95146, 45.15146, -18.64208, -4.84208, 4.00001, 5.8)
    for n in ['base', 'lid']:
        clear(n+' raised display reserve', shapes[n], screen)

    # The shallow cap treatment must preserve the entire sleeve, detents and
    # fit envelope below/above the cosmetic skins, including roll reversibility.
    for end in ['c', 'a']:
        n = 'usb_'+end+'_end_cap'
        cap, original = shapes[n], old[n]
        removed = original.cut(cap)
        b = original.BoundingBox()
        check(n+' only subtractive cosmetics', volume(cap.cut(original)) < TOL)
        skin = box(-40,120,-40,20,b.zmax-.24001,b.zmax+.001).fuse(
            box(-40,120,-40,20,b.zmin-.001,b.zmin+.24001))
        check(n+' cavity and detents unchanged', outside_volume(removed,skin) < TOL)
        rolled = cap.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)
        check(n+' reversible through 180 degree roll', difference(cap,rolled) < TOL)
        for upper in [False, True]:
            mask = box(-40,120,-40,20,1.575 if upper else -20,20 if upper else 1.575)
            cut_skin = removed.intersect(mask)
            backing = cut_skin.translate((0,0,-1.0 if upper else 1.0))
            check(n+' cosmetic floor has 1 mm backing '+str(upper), outside_volume(backing,original) < TOL)
    opposite = shapes['usb_c_end_cap'].rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
    check('Caps retain common geometry at either end',difference(opposite,shapes['usb_a_end_cap']) < TOL)

    # Each grey face panel must have a broad planar bonding interface with the
    # dark carrier. Thin-slice areas are measured from independent exported STEP.
    # This also catches an apparently touching panel connected only at an edge.
    carrier=load(OUT,'lid')
    panel_floor=6.65
    interface_evidence=[]
    for n,s in colors.items():
        if not n.startswith('lid_panel_'): continue
        footprint=s.intersect(box(-30,110,-40,20,panel_floor,panel_floor+.001))
        area=volume(footprint)/.001
        contact=overlap(footprint.translate((0,0,-.001)),carrier)/.001
        fraction=contact/area if area else 0
        check(n+' broad planar bonding interface',area>2 and fraction>.95,
              {'footprint_mm2':area,'backed_mm2':contact,'backed_fraction':fraction})
        # Main panel has intentional apertures; verify material thickness away
        # from their flares with the logo panel as a representative solid roof.
        if n=='lid_panel_logo':
            snap_zones = box(6.05,10.45,0,1.35,6.64,6.66).fuse(
                box(6.05,10.45,-22.55,-21.2,6.64,6.66))
            normal_foot=footprint.cut(snap_zones)
            pocket_foot=footprint.intersect(snap_zones)
            check(n+' normal roof has 0.59 mm carrier below colour floor',
                  outside_volume(normal_foot.translate((0,0,-.59)),carrier)<TOL)
            check(n+' existing snap roof has 0.44 mm carrier below colour floor',
                  outside_volume(pocket_foot.translate((0,0,-.44)),carrier)<TOL)
        interface_evidence.append({'part':n,'footprint_mm2':area,'bonded_mm2':contact})
    # Section probes on the actual recessed joint and along its lower run.
    # Normal L roof begins at Z6.05; final channel floor at6.85 leaves0.80 mm.
    for x,y in [(15.22,0),(14.35,-4.8),(17.48,-8.3),(17.0,-12),(16.6,-16),(16.1,-20)]:
        probe=box(x-.015,x+.015,y-.015,y+.015,6.05001,6.84999)
        check('Channel continuous 0.8 mm roof '+str((x,y)),outside_volume(probe,lid)<TOL)

    # The changes must not alter any of L's four snap posts, roofs or pockets.
    flex_regions = []
    for x0, x1 in [(6.05, 10.45), (69.55, 73.45)]:
        for y0, y1 in [(0, 2.7), (-23.9, -21.2)]:
            region = box(x0, x1, y0, y1, 3.4, 6.65)
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

    check('Face never exceeds L top datum',lid.BoundingBox().zmax <= 7.25001)

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
    for group, members in report.get('ams_groups',{}).items():
        grouped = load(OUT,group)
        expected = load(OUT,members[0])
        for member in members[1:]: expected=expected.fuse(load(OUT,member))
        check(group+' reconstructs named source colours',difference(grouped,expected)<TOL)
    for n in ['lid', *color_names, *report.get('ams_groups',{})]:
        s = load(OUT,n).rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,7.25))
        b = s.BoundingBox(); mesh = trimesh.load(str(OUT/(n+'.stl')), force='mesh')
        check(n+' shared face-down registration', abs(mesh.bounds-[[b.xmin,b.ymin,b.zmin],[b.xmax,b.ymax,b.zmax]]).max() < .03)
    check('Export bytes unchanged during independent verification',
          all(hashlib.sha256((OUT/n).read_bytes()).hexdigest()==h for n,h in export_hashes.items()))
    evidence = {'checks': checks, 'failures': failures, 'export_sha256':export_hashes,
                'panel_interfaces': interface_evidence,
                'basis': 'Independent STEP booleans, sampled closure and control movement, added-material checks against source electronics, STL topology and registration.',
                'limitations': ['Physical print, finish, multi-material adhesion, retention force and fatigue are not simulated.','Interface checks establish geometric bonding area, not inter-material bond strength.']}
    (OUT/'detail-validation.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('Sculpted L:', len(checks), 'checks;', len(failures), 'failures', flush=True)
    assert not failures, failures


if __name__ == '__main__':
    run()
