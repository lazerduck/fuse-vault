"""Revision K: broad D-pad pads, enclosed SD reserve, shorter buttons, PLA sleeve caps.
Run python CaseDesign/build_case_k.py with CadQuery 2.8 and trimesh installed.
F is a read-only baseline. Dimensions are millimetres in PCB coordinates.
"""
from pathlib import Path
import hashlib,json,math,itertools,shutil,zipfile
import cadquery as cq
import trimesh
R=Path(__file__).resolve().parent; O=R/'prototype-k';O.mkdir(exist_ok=True)
F=R/'prototype-f';cx,cy=58.77,-10.38
BOTTOM=-4.1; FLOOR=-2.9; TOP=7.25; CEILING=6.0

def box(x0,x1,y0,y1,z0,z1):
 return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def slab(z0,z1):return box(-30,110,-40,20,z0,z1)
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def sphere(r):return cq.Workplane('XY').sphere(r).translate((cx,cy,2.55)).val()
def bounds(s):
 b=s.BoundingBox();return [b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
def overlap(a,b):
 aa,bb=bounds(a),bounds(b)
 if any(aa[i+1]<=bb[i]+1e-6 or bb[i+1]<=aa[i]+1e-6 for i in (0,2,4)):return 0.
 return max(0.,a.intersect(b).Volume())
def crosspts(span,width):
 a=span/2;b=width/2
 return [(-b,-a),(b,-a),(b,-b),(a,-b),(a,b),(b,b),(b,a),(-b,a),(-b,b),(-a,b),(-a,-b),(-b,-b)]
def cross(span,width,z0,z1):return cq.Workplane('XY').polyline(crosspts(span,width)).close().extrude(z1-z0).translate((cx,cy,z0)).val()
def rounded_x(x0,x1,y,z,w,h,r):return cq.Workplane('YZ',origin=(x0,y,z)).rect(w,h).extrude(x1-x0).edges('|X').fillet(r).val()
source={n:cq.importers.importStep(str(F/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar']}
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in F.iterdir() if p.is_file()}
coords={c['ref']:c['origin_mm'] for c in json.loads((R/'pcb-print-model/measurements.json').read_text())['components'] if c.get('origin_mm')}
# Preserve existing XY board locators, hook and snap geometry. Restore SD shoulder.
outline=box(-2.5,74.8,-23.7,3.9,-10,15)
base=source['base'].intersect(outline).intersect(slab(BOTTOM,15))
# Close floor and side opening, leaving space for the card/socket inside.
base=base.fuse(box(-.6,73.4,-21.85,.7,BOTTOM,FLOOR))
base=base.fuse(box(49.3,69.5,-16.2,3.9,BOTTOM,FLOOR),
 box(49.3,69.5,2.5,3.9,BOTTOM,4.4),
 box(49.3,50.7,.7,3.9,BOTTOM,4.4),
 box(68.1,69.5,1.25,3.9,BOTTOM,4.4))
# Restored card/socket insertion reserve; includes the old F side-bump space.
sd_loading=box(50.7,68.1,-15.,2.5,FLOOR,16)
base=base.cut(sd_loading)
# Keep the two underside service-button holes; USB-C cradle remains intact.
for x,y in [(6.731,-3.302),(6.858,-17.526)]:base=base.cut(cyl(x,y,1.1,-5,-2.3))
# Lower roof 0.55 mm; restore original working button guide interiors exactly.
old=source['lid'].intersect(outline)
lid=old.intersect(slab(-10,CEILING)).fuse(old.intersect(slab(6.55,8)).translate((0,0,-.55)))
for n in ['SW4','SW9']:
 x,y=coords[n];region=cyl(x,y,4.5,3.8,8)
 lid=lid.cut(region).fuse(old.intersect(region).intersect(slab(-10,TOP)))
# The old SD opening extends through the seam. Seal it in both shell halves.
lid=lid.fuse(box(49.3,69.5,2.5,3.9,4.4,TOP))
# Cover the restored shoulder without filling its internal card allowance.
lid=lid.fuse(box(49.3,69.5,.7,3.9,CEILING,TOP))
# Fill the inherited oversized USB-A roof opening, then cut one definitive aperture.
lid=lid.fuse(box(73.4,74.8,-17.8,-4.3,4.4,TOP))
base=base.fuse(box(73.4,74.8,-17.8,-4.3,BOTTOM,4.4))
usb_a_clear=box(68.45,95,-17.65,-4.45,-1.55,5.15)
base=base.cut(usb_a_clear);lid=lid.cut(usb_a_clear)
# Preserve the original USB-C saddle and the full hook-closure collar sweep.
for angle in [0,5,10,15]:
 relief=rounded_x(-5,-.35,-10.668,1.85,14.9,7.9,3.9).rotate((-.5,0,3.2),(-.5,1,3.2),angle)
 lid=lid.cut(relief)
# Keep working button stems and collars; trim only the exposed tops by .55 mm.
buttons={n:source[n].intersect(slab(-10,8.30)).clean() for n in ['select','back']}
# Simple underside-loaded flange. Contact tips sit .1 mm nearer the switches than F;
# heel reaches the same PCB surface, shortening its reach relative to contacts .1 mm.
cap=cross(16,5.5,5.65,8.0)
skirt=cq.Workplane('XY').workplane(offset=4.75).polyline(crosspts(18.2,7.6)).close().workplane(offset=.9).polyline(crosspts(16,5.5)).close().loft().translate((cx,cy,0)).val()
dpad=cap.fuse(skirt,cross(18.2,7.6,4.15,4.77),cyl(cx,cy,.95,2.55,4.2),sphere(.95))
for n in ['SW5','SW6','SW7','SW8']:
 x,y=coords[n];dpad=dpad.fuse(cyl(x,y,1.6,3.65,4.8))
for n in ['SW4','SW9']:
 x,y=coords[n]
 # Keep guide clearance underneath; blend out before the visible thumb face.
 relief=cyl(x,y,4.45,3.8,6.3).fuse(cq.Solid.makeCone(4.45,3.65,.7,cq.Vector(x,y,6.3)))
 dpad=dpad.cut(relief)
dpad=dpad.cut(cyl(cx+6,cy,.45,7.7,8.2)).clean()
patch=cross(18.0,7.5,CEILING,TOP)
for n in ['SW4','SW9']:
 x,y=coords[n]
 # Preserve the actual guide bore, not its oversized construction envelope.
 patch=patch.cut(cyl(x,y,3.45,5.9,6.45)).cut(cyl(x,y,2.5,6.45,8))
lid=lid.fuse(patch).cut(cross(16.7,6.2,5.6,8))
# Four solid roof corners limit lift near the pivot, leaving the arm ends free to tilt.
for sx,sy in itertools.product([-1,1],repeat=2):
 x0,x1=sorted([cx+sx*3.1,cx+sx*4.3]);y0,y1=sorted([cy+sy*3.1,cy+sy*4.3])
 lid=lid.fuse(box(x0,x1,y0,y1,5.7,6.1))
# Continuous closed wall outside the installed-card reserve, not through it.
base=base.clean();lid=lid.clean()
# Fill obsolete off-centre cap pockets; new shallow dimples sit on the centre line.
for x0,x1 in [(.7,3.3),(69.9,72.5)]:
 for y0,y1 in [(2.,2.5),(-23.7,-23.2)]:base=base.fuse(box(x0,x1,y0,y1,-1.8,-.2))
parts={'base':base,'lid':lid,'dpad':dpad,**buttons,'usb_c_collar':source['usb_c_collar']}
# Full PLA sleeves, no narrow tongues. Identical at either end and top/bottom.
z=(BOTTOM+TOP)/2;body_center=(36.15,-10.6,0)
lo,hi=-19.25,2.8
outer=rounded_x(lo,hi,-10.6,z,29.8,TOP-BOTTOM+3.6,1.5)
outer=cq.Workplane(obj=outer).faces('<X').edges().fillet(.5).val()
cover=outer.cut(rounded_x(lo+1.6,hi+1,-10.6,z,26.6,TOP-BOTTOM+.4,.3))
seat=-2.65;start=seat-1.6
for wall_z,tip_z in [(TOP+.2,TOP-1.15),(BOTTOM-.2,BOTTOM+1.15)]:
 stop=cq.Workplane('XZ',origin=(0,-2.6,0)).polyline([(start,wall_z),(seat,tip_z),(seat,wall_z)]).close().extrude(16).val()
 cover=cover.fuse(stop)
# Nominal .10 mm interference, spread through the sleeve wall rather than tabs.
# Height is the shell centre line, unchanged under a 180-degree cap roll.
for yy in [-24.15,2.95]:
 bump=cq.Workplane('XY').sphere(.55).translate((.9,yy,z)).val()
 cover=cover.fuse(bump)
cover=cover.clean()
for end,capshape in [('c',cover),('a',cover.rotate(body_center,(36.15,-10.6,1),180))]:
 parts['usb_'+end+'_end_cap']=capshape
 for yy in [-24.15,2.95]:
  pocket=cq.Workplane('XY').sphere(.70).translate((.9,yy,z)).val()
  if end=='a':pocket=pocket.rotate(body_center,(36.15,-10.6,1),180)
  base=base.cut(pocket)
parts['base']=base.clean();parts['lid']=lid.clean();lid=parts['lid']
# Retain 0.8 mm minimum roof thickness over the unchanged button necks.
print('Shapes built',flush=True)
for n,s in parts.items():
 print(n,s.isValid(),len(s.Solids()),bounds(s),flush=True)
 cq.exporters.export(s,str(O/(n+'.step')))
# Validate functionally important clearances before creating print files.
checks=[];fail=[]
def check(label,a,b):
 v=overlap(a,b);checks.append({'check':label,'overlap_mm3':v})
 if v>1e-4:fail.append(checks[-1])
for n,s in parts.items():
 if not s.isValid() or len(s.Solids())!=1:fail.append({'invalid_shape':n,'solids':len(s.Solids())})
for (n,s),(m,t) in itertools.combinations(parts.items(),2):check(n+' / '+m,s,t)
for axis in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for angle in [-3,-2,-1,0,1,2,3]:
  t=dpad.rotate((cx,cy,2.55),(cx+axis[0],cy+axis[1],2.55),angle)
  for n in ['lid','base','select','back']:check('Dpad tilt '+str(axis)+' '+str(angle)+' / '+n,t,parts[n])
for n in ['select','back']:
 for dz in [0,-.2,-.4,-.6]:check(n+' travel '+str(dz),parts[n].translate((0,0,dz)),lid)
# Positive capture: a displaced cap must meet the guide before escaping.
retention=[]
for shift in [(0,0,.4),(.45,0,0),(-.45,0,0),(0,.45,0),(0,-.45,0)]:
 v=overlap(dpad.translate(shift),lid);retention.append({'shift_mm':shift,'interference_mm3':v})
 if v<1e-4:fail.append({'retention_missing':shift})
# Verify the actual assembly path: D-pad inserted upward from beneath detached lid.
for dz in [-12,-8,-5,-3,-2,-1,-.5,-.2,0]:check('Dpad insertion '+str(dz),dpad.translate((0,0,dz)),lid)
print('Movement checks',json.dumps(fail),flush=True)
if fail: raise RuntimeError('Fix movement before electronics validation')
import sys
if '--geometry-only' in sys.argv:sys.exit(0)
board_path=R/'pcb-print-model/source-board.step'
cache=Path('/tmp/fuse-vault-case-'+hashlib.sha256(board_path.read_bytes()).hexdigest()[:12]+'.brep')
if cache.exists():reference=cq.Shape.importBrep(str(cache))
else:
 reference=cq.importers.importStep(str(board_path)).val();reference.exportBrep(str(cache))
electronics=reference.Solids()
for n,s in parts.items():
 print('Electronics',n,flush=True)
 for i,e in enumerate(electronics):check(n+' / electronic '+str(i),s,e)
# Preserve the display reserve used by F. Installed flex depth remains a dry-fit check.
for n,s in parts.items():check(n+' / display',s,box(17.10146,45.00146,-18.49208,-4.99208,2.1,4.6))
# Loading of the board/socket is vertical; the closed SD side is never an access route.
check('board insertion envelope',base,box(-.127,73.208,-21.237,0,.001,16))
check('restored SD loading envelope',base,sd_loading)
card_reserve=box(50.7,68.1,-15,2.5,-2.7,0)
for n in ['base','lid']:check('installed SD card/socket reserve / '+n,parts[n],card_reserve)
for dx in [-.5,.5]:
 if sum(overlap(parts['usb_c_collar'].translate((dx,0,0)),parts[n]) for n in ['base','lid'])<1e-4:fail.append({'collar_capture_missing':dx})
report={'revision':'K','body_mm':[77.3,27.6,TOP-BOTTOM],'main_body_width_mm':26.2,'previous_F_body_mm':[77.3,27.6,12.1],
 'overall_height_with_buttons_mm':8.30-BOTTOM,'round_button_protrusion_mm':1.05,'dpad_contact_diameter_mm':3.2,'sd_loading_bounds_mm':bounds(sd_loading),'cap_detent_z_mm':z,'cap_nominal_interference_mm':.1,'dpad_pivot':[cx,cy,2.55],
 'dpad_contact_tip_z_mm':3.65,'switch_library_top_z_mm':3.562429073603,'dpad_contact_gap_mm':.087570926397,'dpad_opening_side_gap_mm':.35,
 'usb_a_opening_height_mm':6.7,'previous_union_opening_height_mm':8.3,
 'floor_top_z_mm':FLOOR,'floor_thickness_mm':1.2,'checks':checks,'retention':retention,'failures':fail,
 'limitations':['Physical print fit and actuation pending.','Floor restored to Z=-2.9; dry-fit actual folded display flex before closing.','Library geometry is not measured hardware.','D-pad operational clearance checked to 3 degrees; switch compression is not simulated.']}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('FAILURES',json.dumps(fail,indent=2),flush=True)
if fail:raise RuntimeError('Review failures before exporting STLs')
for n,s in parts.items():
 if n=='usb_c_collar':
  shutil.copyfile(F/(n+'.stl'),O/(n+'.stl'));continue
 p=s
 if n in ['lid','dpad','select','back']:p=p.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':p=p.rotate((0,0,0),(0,1,0),-90)
 elif n=='usb_a_end_cap':p=p.rotate((0,0,0),(0,1,0),90)
 b=p.BoundingBox();p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(O/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(str(O/(n+'.stl')),force='mesh');mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 assert mesh.is_watertight and len(mesh.split())==1,n
 mesh.export(str(O/(n+'.stl')))
 report.setdefault('meshes',{})[n]={'watertight':True,'single_body':True,'size_mm':mesh.extents.tolist()}
a=cq.Assembly(name='Fuse_Vault_K')
for n,s in parts.items():a.add(s,name=n,color=cq.Color(*((.9,.62,.2) if n in ['dpad','select','back'] else (.55,.64,.73))))
a.export(str(O/'case-assembly.step'))
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(hashlib.sha256((F/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
print('Validated and exported K',flush=True)
