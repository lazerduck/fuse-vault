"""Panelled L v2: a tangent SD-shoulder blend and restrained face detailing.
Read-only L inputs. All exports go to prototype-l-panelled-v2.
"""
from pathlib import Path
import json, shutil
import cadquery as cq
from build_case_l_styles import (ROOT, BASE, NAMES, TOP, box, poly, volume,
    intersection_volume, difference, digest, print_shape, export_mesh)
OUT=ROOT/'prototype-l-panelled-v2'
OUT.mkdir(exist_ok=True)
INLAY=.36

def contour(z, inset=0):
    """Cubic Bezier: horizontal tangents at both ends of the shoulder rise."""
    w=(cq.Workplane('XY',origin=(0,0,z)).moveTo(41,2.5-inset)
       .bezier([(45,2.5-inset),(45.8,3.9-inset),(49.8,3.9-inset)],includeCurrent=True)
       .lineTo(50.3,3.9-inset).lineTo(50.3,1.9).lineTo(41,1.9).close())
    return w.val()

def shoulder():
    # Ruled loft preserves the original .55 mm 45-degree top/bottom easing.
    wires=[contour(-4.1,.55),contour(-3.55),contour(6.7),contour(7.25,.55)]
    return cq.Solid.makeLoft(wires,ruled=True)

def facefield():
    # Inset face follows the new smooth outer profile, leaving a fine dark rim.
    return (cq.Workplane('XY').moveTo(3.6,-22.65).lineTo(68.7,-22.65)
       .lineTo(68.7,2.85).lineTo(49.8,2.85)
       .bezier([(45.8,2.85),(45,1.45),(41,1.45)],includeCurrent=True)
       .lineTo(3.6,1.45).close().extrude(.5).translate((0,0,TOP-INLAY)).val())

def rounded_rectangle(w,h,r,z,height):
    return (cq.Workplane('XY').center(32.45146,-11.74208).rect(w,h)
       .extrude(height).edges('|Z').fillet(r).translate((0,0,z)).val())

source_hashes={n+ext:digest(BASE/(n+ext)) for n in NAMES for ext in ['.step','.stl']}
p={n:cq.importers.importStep(str(BASE/(n+'.step'))).val() for n in NAMES}
checks=[]
def check(label,ok):
    checks.append({'check':label,'passed':bool(ok)})
    assert ok,label
blend=shoulder()
for n,z0,z1 in [('base',-4.1,4.4),('lid',4.4,7.25)]:
    p[n]=p[n].fuse(blend.intersect(box(40,51,1.8,4,-5 if n=='base' else z0,z1))).clean()
    check(n+' blended solid',p[n].isValid() and len(p[n].Solids())==1)
    original=cq.importers.importStep(str(BASE/(n+'.step'))).val()
    check(n+' no original material removed by shoulder',volume(original.cut(p[n]))<1e-5)
plain=p['lid']
# Logo: original polygons, 80% scale and the established clear spacing.
cx,cy,scale=9.,-10.6,.8
arm=[(.65,7),(.65,4.3),(-3.6,0),(-1.55,-2.05),(-2.95,-3.45),(-5.65,-.75),(-5.65,1.25)]
logo=poly([(cx+scale*x,cy+scale*y) for x,y in arm],TOP-INLAY,INLAY).fuse(
     poly([(cx-scale*x,cy-scale*y) for x,y in arm],TOP-INLAY,INLAY))
diamond=poly([(cx-1.65*scale,cy),(cx,cy+1.65*scale),(cx+1.65*scale,cy),(cx,cy-1.65*scale)],TOP-INLAY,INLAY)
# One slightly raked seam, with a local six-sided keyed orange recess.
seam_pts=[(14.45,2.7),(17.05,-23.1),(17.65,-23.1),(15.05,2.7)]
accent_pts=[(14.75,-9.5),(16.85,-9.5),(17.65,-10.3),(17.65,-11.5),(15.55,-11.5),(14.75,-10.7)]
frame_pts=[(14.4,-9.15),(17.,-9.15),(18.,-10.15),(18.,-11.85),(15.4,-11.85),(14.4,-10.85)]
groove=poly(seam_pts,TOP-.24,.34).fuse(poly(frame_pts,TOP-.24,.34))
sculpted=plain.cut(groove).clean()
topband=box(-3,76,-25,5,TOP-INLAY,TOP)
perimeter=plain.intersect(topband).cut(facefield())
# Screen bevel is dark through its full visible depth; aperture is unchanged.
screen=rounded_rectangle(25.6,14.7,.75,6.4,.85)
dark=perimeter.fuse(logo,screen,poly(seam_pts,TOP-INLAY,INLAY),poly(frame_pts,TOP-INLAY,INLAY))
orange=diamond.fuse(poly(accent_pts,TOP-INLAY,INLAY)).intersect(sculpted).clean()
dark=dark.intersect(sculpted).cut(orange).clean()
p['lid']=sculpted.cut(dark).cut(orange).clean()
p['lid_inlay_dark']=dark; p['lid_inlay_orange']=orange
joined=p['lid'].fuse(dark,orange).clean()
check('Colour union reconstructs sculpted shell',difference(joined,sculpted)<1e-5)
check('Lid structural material remains connected',p['lid'].isValid() and len(p['lid'].Solids())==1)
check('No added material over original aperture',volume(joined.cut(plain))<1e-5)
for n in ['lid_inlay_dark','lid_inlay_orange']:
    check(n+' no gaps or overlaps with structural lid',volume(p[n].intersect(p['lid']))<1e-5)
check('Inlay colours disjoint',volume(dark.intersect(orange))<1e-5)
# Geometric continuity at the SD shoulder, sampled just inside each surface.
# Verify the plan outline transitions smoothly instead of retaining the old step.
profile=[]
for x in [40.9,41,43,45,47,49,49.3,49.5,49.8,50.2,50.4]:
    strip=p['base'].intersect(box(x-.001,x+.001,1.9,5,0,1))
    profile.append({'x':x,'outer_y':strip.BoundingBox().ymax})
colors={n:(.19,.215,.24) for n in p}
colors.update(lid=(.68,.72,.75),lid_inlay_dark=(.13,.15,.17),
    lid_inlay_orange=(1,.43,.025),select=(1,.43,.025),
    dpad=(.13,.15,.17),back=(.13,.15,.17),screen_shim=(.8,.6,.2),
    usb_c_collar=(.6,.63,.65),usb_c_end_cap=(.23,.25,.28),usb_a_end_cap=(.23,.25,.28))
meshes={}
for n,s in p.items():
    check(n+' valid',s.isValid())
    cq.exporters.export(s,str(OUT/(n+'.step')))
    meshes[n]=export_mesh(print_shape(n,s),OUT/(n+'.stl'))
cq.exporters.export(joined,str(OUT/'lid_single_colour.step'))
meshes['lid_single_colour']=export_mesh(print_shape('lid',joined),OUT/'lid_single_colour.stl')
shutil.copy2(OUT/'usb_c_end_cap.stl',OUT/'universal_end_cap.stl')
assembly=cq.Assembly(name='Fuse_Vault_L_panelled_v2')
ams=cq.Assembly(name='L_panelled_v2_AMS_lid')
for n,s in p.items():
    assembly.add(s,name=n,color=cq.Color(*colors[n]))
    if n.startswith('lid'): ams.add(print_shape(n,s),name=n,color=cq.Color(*colors[n]))
assembly.export(str(OUT/'case-assembly.step')); ams.export(str(OUT/'lid-ams.step'))
check('L source files unchanged',all(digest(BASE/n)==h for n,h in source_hashes.items()))
report={'style':'panelled-v2','baseline':'prototype-l','checks':checks,'failures':[],
    'source_sha256':source_hashes,'meshes':meshes,'colors':colors,
    'shoulder_profile_samples':profile,'blend_extent_x_mm':[41,50.3],
    'blend_control_points_mm':[[41,2.5],[45,2.5],[45.8,3.9],[49.8,3.9]],
    'logo_scale':scale,'logo_centre_mm':[cx,cy],'inlay_depth_mm':INLAY,
    'screen_bevel_colour_depth_mm':.85,'seam_depth_mm':.24,
    'limitations':['New base and lid must be printed together for the matching shoulder curve.','CAD clearances do not qualify printing, colour adhesion, snap force or fatigue.']}
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Panelled v2 built;',len(checks),'checks',flush=True)
