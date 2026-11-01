"""Cosmetic alternatives on L: preserve its fit, use the original Vault polygons.
Run with CadQuery 2.8.0 and trimesh. Inputs are read-only prototype-l STEP/STL files.
"""
from pathlib import Path
import hashlib
import itertools
import json
import shutil
import cadquery as cq
import trimesh

ROOT = Path(__file__).resolve().parent
BASE = ROOT / 'prototype-l'
NAMES = ['base', 'lid', 'dpad', 'select', 'back', 'screen_shim',
         'usb_c_collar', 'usb_c_end_cap', 'usb_a_end_cap']
TOP = 7.25
INLAY = .36
SEAM_DEPTH = .24

def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane('XY').box(x1-x0, y1-y0, z1-z0, centered=False).translate((x0,y0,z0)).val()

def poly(points, z=TOP-INLAY, height=INLAY):
    return cq.Workplane('XY').polyline(points).close().extrude(height).translate((0,0,z)).val()

def volume(s):
    return max(0., s.Volume()) if s.Solids() else 0.

def intersection_volume(a,b):
    return volume(a.intersect(b)) if a.Solids() and b.Solids() else 0.

def difference(a, b):
    return volume(a.cut(b)) + volume(b.cut(a))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def cap_rotate(s):
    return s.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)

def cap_roll(s):
    return s.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)

def print_shape(name, s):
    if name.startswith('lid'):
        # A single shared transform is essential for the disconnected inlay bodies.
        return s.rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,TOP))
    if name in ['dpad','select','back']:
        s=s.rotate((0,0,0),(1,0,0),180)
    elif name in ['usb_c_end_cap','universal_end_cap']:
        s=s.rotate((0,0,0),(0,1,0),-90)
    elif name in ['usb_a_end_cap','usb_c_collar']:
        s=s.rotate((0,0,0),(0,1,0),90)
    b=s.BoundingBox()
    return s.translate((-b.xmin,-b.ymin,-b.zmin))

def export_mesh(s, path):
    cq.exporters.export(s, str(path), tolerance=.015, angularTolerance=.1)
    mesh=trimesh.load(str(path), force='mesh')
    mesh.update_faces(mesh.nondegenerate_faces())
    mesh.remove_unreferenced_vertices(); mesh.merge_vertices()
    assert mesh.is_watertight and mesh.is_winding_consistent, path
    assert all(m.volume > 0 for m in mesh.split()), path
    mesh.export(str(path))
    return {'watertight':True, 'components':len(mesh.split()), 'volume_mm3':float(mesh.volume)}


def build(style):
    out=ROOT / ('prototype-l-'+style); out.mkdir(exist_ok=True)
    source_hashes={n+ext:digest(BASE/(n+ext)) for n in NAMES for ext in ['.step','.stl']}
    p={n:cq.importers.importStep(str(BASE/(n+'.step'))).val() for n in NAMES}
    plain=p['lid']; checks=[]
    def check(label, ok):
        checks.append({'check':label, 'passed':bool(ok)})
        assert ok, label
    # Original G brand outline, uniformly scaled to leave space beside the cap.
    cx,cy=9.0,-10.6
    logo_scale=.8
    arm=[(.65,7),(.65,4.3),(-3.6,0),(-1.55,-2.05),(-2.95,-3.45),(-5.65,-.75),(-5.65,1.25)]
    arms=poly([(cx+logo_scale*x,cy+logo_scale*y) for x,y in arm]).fuse(poly([(cx-logo_scale*x,cy-logo_scale*y) for x,y in arm]))
    orange=poly([(cx-1.65*logo_scale,cy),(cx,cy+1.65*logo_scale),(cx+1.65*logo_scale,cy),(cx,cy-1.65*logo_scale)])
    sculpted=plain
    dark=arms
    if style=='panelled':
        # One stepped diagonal, clear of logo, screen and four snap roofs.
        points=[(15.6,2.8),(13.2,-5),(17.2,-12),(13.6,-23.1),
                (14.4,-23.1),(18,-12),(14,-5),(16.4,2.8)]
        groove=poly(points,TOP-SEAM_DEPTH,SEAM_DEPTH+.1)
        sculpted=plain.cut(groove).clean()
        dark=dark.fuse(poly(points))
        accent=poly([(14.05,-3.2),(14.6,-1.6),(15.3,-1.6),(14.75,-3.2)])
        orange=orange.fuse(accent)
        for x0,x1 in [(6.05,10.45),(69.55,73.45)]:
            for y0,y1 in [(0,2.7),(-23.9,-21.2)]:
                check('Snap roof untouched '+str((x0,y0)),volume(groove.intersect(box(x0,x1,y0,y1,6.2,7.4)))<1e-6)
    orange=orange.intersect(sculpted).clean()
    dark=dark.intersect(sculpted).cut(orange).clean()
    p['lid']=sculpted.cut(dark).cut(orange).clean()
    p['lid_inlay_dark']=dark; p['lid_inlay_orange']=orange
    joined=p['lid'].fuse(dark,orange).clean()
    check('Colour bodies reconstruct cosmetic lid',difference(joined,sculpted)<1e-5)
    check('No added material outside L lid',volume(joined.cut(plain))<1e-5)
    check('L geometry below cosmetic layer unchanged',intersection_volume(plain.cut(joined),box(-30,110,-40,20,-15,TOP-INLAY))<1e-5)
    check('Structural lid remains one solid',len(p['lid'].Solids())==1)
    # A full 0.75 mm backing under every colour pocket; avoids floating inlays.
    for name in ['lid_inlay_dark','lid_inlay_orange']:
        backing=p[name].translate((0,0,-.75))
        check(name+' has continuous backing',volume(backing.cut(plain))<1e-5)
    for a,b in itertools.combinations(['lid','lid_inlay_dark','lid_inlay_orange'],2):
        check(a+' / '+b+' disjoint',volume(p[a].intersect(p[b]))<1e-5)
    if style=='precision':
        cap=p['usb_c_end_cap']; b=cap.BoundingBox()
        # Three shallow rounded channels on both broad faces. Avoid side detents.
        cuts=[]
        for y in [-17.1,-10.6,-4.1]:
            cut=cq.Workplane('XY').center((b.xmin+b.xmax)/2,y).slot2D(b.xlen-4,1.0,0).extrude(.30).translate((0,0,b.zmax-.24)).val()
            cuts.extend([cut,cap_roll(cut)])
            # Groove floor must retain at least 1 mm of material beneath it.
            floor=cut.translate((0,0,-1.0))
            check('Cap groove backed '+str(y),volume(floor.cut(cap))<1e-5)
            check('Cap reverse groove backed '+str(y),volume(cap_roll(floor).cut(cap))<1e-5)
        new=cap.cut(*cuts).clean()
        check('Cap remains one valid solid',new.isValid() and len(new.Solids())==1)
        check('Cap roll symmetry',difference(new,cap_roll(new))<1e-5)
        check('Cap only loses external texture',volume(new.cut(cap))<1e-5)
        # Preserve the entire lateral sleeve walls and bead-bearing regions.
        for y0,y1 in [(-30,-21),(0,8)]:
            check('Cap retention side untouched '+str(y0),intersection_volume(cap.cut(new),box(-30,10,y0,y1,-20,20))<1e-5)
        p['usb_c_end_cap']=new; p['usb_a_end_cap']=cap_rotate(new)
    colors={n:(.10,.115,.13) for n in p}
    colors.update(usb_c_end_cap=(.24,.27,.30), usb_a_end_cap=(.24,.27,.30),
                  dpad=(.16,.18,.20), back=(.16,.18,.20), lid=(.42,.48,.54) if style=='precision' else (.73,.76,.78),
                  select=(1,.43,.025), lid_inlay_orange=(1,.43,.025), screen_shim=(.8,.6,.2))
    meshes={}
    for n,s in p.items():
        check(n+' valid',s.isValid())
        if n not in ['lid','lid_inlay_dark','lid_inlay_orange','usb_c_end_cap','usb_a_end_cap']:
            shutil.copy2(BASE/(n+'.step'),out/(n+'.step'))
            shutil.copy2(BASE/(n+'.stl'),out/(n+'.stl'))
            check(n+' source preserved',digest(out/(n+'.step'))==source_hashes[n+'.step'])
        else:
            cq.exporters.export(s,str(out/(n+'.step')))
        meshes[n]=export_mesh(print_shape(n,s),out/(n+'.stl'))
    cq.exporters.export(joined,str(out/'lid_single_colour.step'))
    meshes['lid_single_colour']=export_mesh(print_shape('lid',joined),out/'lid_single_colour.stl')
    shutil.copy2(out/'usb_c_end_cap.stl',out/'universal_end_cap.stl')
    assembly=cq.Assembly(name='Fuse_Vault_L_'+style)
    ams=cq.Assembly(name='L_'+style+'_AMS_lid')
    for n,s in p.items():
        assembly.add(s,name=n,color=cq.Color(*colors[n]))
        if n.startswith('lid'):
            ams.add(print_shape(n,s),name=n,color=cq.Color(*colors[n]))
    assembly.export(str(out/'case-assembly.step')); ams.export(str(out/'lid-ams.step'))
    check('All L source files unchanged',all(digest(BASE/n)==h for n,h in source_hashes.items()))
    report={'style':style,'baseline':'prototype-l','checks':checks,'failures':[],
            'source_sha256':source_hashes,'meshes':meshes,'colors':colors,
            'logo_scale':logo_scale,'logo_centre_mm':[cx,cy],'inlay_depth_mm':INLAY,'seam_depth_mm':SEAM_DEPTH if style=='panelled' else 0,
            'cap_groove_depth_mm':.24 if style=='precision' else 0,
            'limitations':['CAD preservation does not qualify colour adhesion, surface finish or cap fatigue. Physical print trial required.']}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(style, len(checks),'checks passed',flush=True)

if __name__=='__main__':
    for style in ['precision','panelled']:
        build(style)
