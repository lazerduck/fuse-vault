"""Sculpted panel study on L: true relief, bevelled panels, original logo.
Face cosmetics stay at/below L's established top plane; internals are unchanged.
"""
import json, shutil
import cadquery as cq
from build_case_l_styles import (ROOT, BASE, NAMES, TOP, box, poly, volume,
    intersection_volume, difference, digest, print_shape, export_mesh)
OUT=ROOT/'prototype-l-sculpted'
OUT.mkdir(exist_ok=True)
PANEL_FLOOR=6.65
CHANNEL_FLOOR=6.85
PANEL_BEVEL=.30


def shoulder_contour(z,inset=0):
    return (cq.Workplane('XY',origin=(0,0,z)).moveTo(41,2.5-inset)
      .bezier([(45,2.5-inset),(45.8,3.9-inset),(49.8,3.9-inset)],includeCurrent=True)
      .lineTo(50.3,3.9-inset).lineTo(50.3,1.9).lineTo(41,1.9).close().val())

def field_outline():
    return (cq.Workplane('XY').moveTo(3.55,-22.65).lineTo(68.7,-22.65)
      .lineTo(68.7,2.85).lineTo(49.8,2.85)
      .bezier([(45.8,2.85),(45,1.45),(41,1.45)],includeCurrent=True)
      .lineTo(3.55,1.45).close())

def rounded_panel(points):
    # Vertical rounding softens panel corners; a separate bevel provides depth.
    shape=poly(points,PANEL_FLOOR,TOP-PANEL_FLOOR)
    shape=cq.Workplane(obj=shape).edges('|Z').fillet(.35).val()
    return cq.Workplane(obj=shape).faces('>Z').edges().chamfer(PANEL_BEVEL).val()

source_hashes={n+ext:digest(BASE/(n+ext)) for n in NAMES for ext in ['.step','.stl']}
p={n:cq.importers.importStep(str(BASE/(n+'.step'))).val() for n in NAMES}
checks=[]
def check(label,ok):
    checks.append({'check':label,'passed':bool(ok)})
    assert ok,label
blend=cq.Solid.makeLoft([shoulder_contour(-4.1,.55),shoulder_contour(-3.55),
                        shoulder_contour(6.7),shoulder_contour(7.25,.55)],ruled=True)
for name,z0,z1 in [('base',-4.1,4.4),('lid',4.4,7.25)]:
    p[name]=p[name].fuse(blend.intersect(box(40,51,1.8,4,z0,z1))).clean()
    check(name+' blended single solid',p[name].isValid() and len(p[name].Solids())==1)
plain=p['lid']
# Two broad panels separated by a single keyed dogleg; a smaller corner facet
# picks up the same 45-degree language beside the controls.
logo_pts=[(3.55,1.45),(14.85,1.45),(13.95,-5.3),(17.1,-8.45),
          (15.6,-22.65),(3.55,-22.65)]
# The main panel follows the same tangent curve as the smooth SD shoulder.
main_wire=(cq.Workplane('XY').moveTo(15.6,1.45).lineTo(41,1.45)
  .bezier([(45,1.45),(45.8,2.85),(49.8,2.85)],includeCurrent=True)
  .lineTo(63.4,2.85).lineTo(68.7,-2.45).lineTo(68.7,-22.65)
  .lineTo(16.35,-22.65).lineTo(17.85,-8.15).lineTo(14.7,-5.0).close())
main_shape=main_wire.extrude(TOP-PANEL_FLOOR).translate((0,0,PANEL_FLOOR)).val()
main_shape=cq.Workplane(obj=main_shape).edges('|Z').fillet(.6).val()
main_shape=cq.Workplane(obj=main_shape).faces('>Z').edges().chamfer(PANEL_BEVEL).val()
logo_shape=rounded_panel(logo_pts)
corner_shape=rounded_panel([(64.45,2.85),(68.7,2.85),(68.7,-1.4)])
# Relief everywhere between the panels; the dark perimeter stays bed-contacting.
field=field_outline().extrude(TOP-CHANNEL_FLOOR+.01).translate((0,0,CHANNEL_FLOOR)).val()
chassis=plain.cut(field).clean()
panels={'lid_panel_logo':logo_shape.intersect(plain).clean(),
        'lid_panel_main':main_shape.intersect(plain).clean(),
        'lid_panel_corner':corner_shape.intersect(plain).clean()}
# Use a narrow rounded outer screen lip: grey panel chamfer outside, charcoal
# down the existing flared opening. All operations remove only exterior roof.
window_cx,window_cy=32.45146,-11.74208
screen_mask=(cq.Workplane('XY').center(window_cx,window_cy).rect(24.85,13.95)
             .extrude(.85).edges('|Z').fillet(.45).translate((0,0,6.4)).val())
# Exactly reconstruct existing logo, modestly resized for clear margins.
cx,cy,scale=8.8,-10.8,.75
arm=[(.65,7),(.65,4.3),(-3.6,0),(-1.55,-2.05),(-2.95,-3.45),(-5.65,-.75),(-5.65,1.25)]
logo=poly([(cx+scale*x,cy+scale*y) for x,y in arm],TOP-.36,.36).fuse(
     poly([(cx-scale*x,cy-scale*y) for x,y in arm],TOP-.36,.36))
diamond=poly([(cx-1.65*scale,cy),(cx,cy+1.65*scale),
              (cx+1.65*scale,cy),(cx,cy-1.65*scale)],TOP-.36,.36)
# Orange insert follows the diagonal keyed joint. Its ends tuck into the panels,
# so it reads as an integral tongue. Its face contacts the bed with the panels.
accent_pts=[(14.22,-5.64),(14.83,-5.03),(17.57,-7.77),(16.96,-8.38)]
accent=poly(accent_pts,PANEL_FLOOR,TOP-PANEL_FLOOR)
accent=cq.Workplane(obj=accent).faces('>Z').edges().chamfer(.14).val()
# Add panels to the dark carrier before assigning surface colour bodies.
assembled=chassis.fuse(*panels.values(),accent).clean()
check('No added volume above L shell envelope',volume(assembled.cut(plain))<1e-5)
orange=diamond.intersect(assembled).fuse(accent).clean()
dark=logo.fuse(screen_mask).intersect(assembled).cut(orange).clean()
for n,s in panels.items():
    panels[n]=s.cut(dark).cut(orange).clean()
carrier=assembled.cut(cq.Compound.makeCompound(list(panels.values()))).cut(dark).cut(orange).clean()
p['lid']=carrier
p.update(panels)
p['lid_inlay_dark']=dark
p['lid_inlay_orange']=orange
lid_names=[n for n in p if n.startswith('lid')]
joined=p['lid'].fuse(*(p[n] for n in lid_names if n!='lid')).clean()
check('Multipart lid reconstructs final relief',difference(joined,assembled)<1e-5)
check('Carrier connected',p['lid'].isValid() and len(p['lid'].Solids())==1)
check('Joined lid connected',joined.isValid() and len(joined.Solids())==1)
for i,n in enumerate(lid_names):
    check(n+' valid',p[n].isValid())
    for m in lid_names[i+1:]:check(n+' / '+m+' disjoint',intersection_volume(p[n],p[m])<1e-5)
# Ease the thumb-contacting rims only. Contacts, guide stems, captive flanges
# and the D-pad orientation dot retain their original geometry.
top_face=cq.Workplane(obj=p['dpad']).faces('>Z').val()
p['dpad']=cq.Workplane(obj=p['dpad']).newObject(top_face.outerWire().Edges()).chamfer(.20).val()
for name in ['select','back']:
    p[name]=cq.Workplane(obj=p[name]).faces('>Z').edges().fillet(.20).val()
    check(name+' eased top valid',p[name].isValid())
check('Dpad eased top valid',p['dpad'].isValid())
# Matching shallow cap-face relief makes the end sleeves part of the surface
# design. Both broad faces are identical, so the caps remain reversible.
cap=p['usb_c_end_cap']
cut=(cq.Workplane('XY').center(-8.225,-10.6).rect(18.05,25.8).extrude(.44)
     .edges('|Z').fillet(1.2).faces('<Z').edges().chamfer(.24)
     .translate((0,0,8.81)).val())
rolled=cut.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)
p['usb_c_end_cap']=cap.cut(cut,rolled).clean()
p['usb_a_end_cap']=p['usb_c_end_cap'].rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
check('Cap relief remains connected',p['usb_c_end_cap'].isValid() and len(p['usb_c_end_cap'].Solids())==1)
check('Cap relief only subtracts material',volume(p['usb_c_end_cap'].cut(cap))<1e-5)
# A generous colour separation leaves the chassis/body and screen surround dark.
colors={n:(.16,.18,.20) for n in p}
colors.update(lid=(.16,.18,.20),lid_inlay_dark=(.16,.18,.20),
    lid_panel_logo=(.72,.74,.73),lid_panel_main=(.72,.74,.73),
    lid_panel_corner=(.72,.74,.73),lid_inlay_orange=(1,.46,.025),
    select=(1,.46,.025),dpad=(.16,.18,.20),back=(.16,.18,.20),
    screen_shim=(.8,.6,.2),usb_c_collar=(.6,.63,.65),
    usb_c_end_cap=(.16,.18,.20),usb_a_end_cap=(.16,.18,.20))
meshes={}
for n,s in p.items():
    check(n+' solid valid',s.isValid())
    cq.exporters.export(s,str(OUT/(n+'.step')))
    meshes[n]=export_mesh(print_shape(n,s),OUT/(n+'.stl'))
cq.exporters.export(joined,str(OUT/'lid_single_colour.step'))
meshes['lid_single_colour']=export_mesh(print_shape('lid',joined),OUT/'lid_single_colour.stl')
shutil.copy2(OUT/'usb_c_end_cap.stl',OUT/'universal_end_cap.stl')
groups={'lid_ams_charcoal':['lid','lid_inlay_dark'],
        'lid_ams_grey':['lid_panel_logo','lid_panel_main','lid_panel_corner'],
        'lid_ams_orange':['lid_inlay_orange']}
group_colors={'lid_ams_charcoal':colors['lid'],'lid_ams_grey':colors['lid_panel_main'],
              'lid_ams_orange':colors['lid_inlay_orange']}
group_shapes={}
for n,members in groups.items():
    g=p[members[0]]
    if len(members)>1:g=g.fuse(*(p[m] for m in members[1:])).clean()
    check(n+' grouped colour valid',g.isValid())
    group_shapes[n]=g
    cq.exporters.export(g,str(OUT/(n+'.step')))
    meshes[n]=export_mesh(print_shape('lid',g),OUT/(n+'.stl'))
assembly=cq.Assembly(name='Fuse_Vault_L_sculpted')
ams=cq.Assembly(name='L_sculpted_AMS_lid')
for n,s in p.items():
    assembly.add(s,name=n,color=cq.Color(*colors[n]))
for n,s in group_shapes.items():
    ams.add(print_shape('lid',s),name=n,color=cq.Color(*group_colors[n]))
assembly.export(str(OUT/'case-assembly.step'));ams.export(str(OUT/'lid-ams.step'))
check('Read-only L inputs unchanged',all(digest(BASE/n)==h for n,h in source_hashes.items()))
report={'style':'sculpted','baseline':'prototype-l','checks':checks,'failures':[],
    'source_sha256':source_hashes,'meshes':meshes,'colors':colors,
    'ams_groups':groups,'ams_colors':group_colors,
    'panel_material_floor_mm':PANEL_FLOOR,'channel_floor_mm':CHANNEL_FLOOR,
    'panel_edge_bevel_mm':PANEL_BEVEL,'face_height_mm':TOP,
    'control_top_edge_easing_mm':.20,'cap_face_relief_mm':.24,'logo_scale':scale,'logo_centre_mm':[cx,cy],
    'limitations':['Physical multi-material print, face-down bridges and surface finish need trial.']}
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Sculpted CAD built;',len(checks),'checks',flush=True)
