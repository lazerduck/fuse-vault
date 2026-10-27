"""Revision I: enclosed SD, lower shell, unchanged F buttons, captured D-pad.
Run python CaseDesign/build_case_i.py with CadQuery 2.8 and trimesh installed.
F is a read-only baseline. Dimensions are millimetres in PCB coordinates.
"""
from pathlib import Path
import hashlib,json,math,itertools,shutil,zipfile
import cadquery as cq
import trimesh
R=Path(__file__).resolve().parent; O=R/'prototype-i';O.mkdir(exist_ok=True)
F=R/'prototype-f';cx,cy=58.77,-10.38
BOTTOM=-3.6; FLOOR=-2.4; TOP=7.25; CEILING=6.0

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
# Preserve existing XY board locators, hook and snap geometry. Remove SD shoulder.
outline=box(-2.5,74.8,-23.7,2.5,-10,15)
base=source['base'].intersect(outline).intersect(slab(BOTTOM,15))
# Close floor and side opening, leaving space for the card/socket inside.
base=base.fuse(box(-.6,73.4,-21.85,.7,BOTTOM,FLOOR))
base=base.fuse(box(48.5,68.2,.7,2.5,BOTTOM,4.4))
# Keep the two underside service-button holes; USB-C cradle remains intact.
for x,y in [(6.731,-3.302),(6.858,-17.526)]:base=base.cut(cyl(x,y,1.1,-5,-2.3))
# Lower roof 0.55 mm; restore original working button guide interiors exactly.
old=source['lid'].intersect(outline)
lid=old.intersect(slab(-10,CEILING)).fuse(old.intersect(slab(6.55,8)).translate((0,0,-.55)))
for n in ['SW4','SW9']:
 x,y=coords[n];region=cyl(x,y,4.5,3.8,8)
 lid=lid.cut(region).fuse(old.intersect(region).intersect(slab(-10,TOP)))
# The old SD opening extends through the seam. Seal it in both shell halves.
lid=lid.fuse(box(50,68,.7,2.5,4.4,TOP))
# Fill the inherited oversized USB-A roof opening, then cut one definitive aperture.
lid=lid.fuse(box(73.4,74.8,-17.8,-4.3,4.4,TOP))
base=base.fuse(box(73.4,74.8,-17.8,-4.3,BOTTOM,4.4))
usb_a_clear=box(68.45,95,-17.65,-4.45,-1.55,5.15)
base=base.cut(usb_a_clear);lid=lid.cut(usb_a_clear)
# Preserve the original USB-C saddle and the full hook-closure collar sweep.
for angle in [0,5,10,15]:
 relief=rounded_x(-5,-.35,-10.668,1.85,14.9,7.9,3.9).rotate((-.5,0,3.2),(-.5,1,3.2),angle)
 lid=lid.cut(relief)
# The working buttons are copied with no geometry or position change.
# Captured spherical dome: lower heel rests on PCB, upper socket locates and
# limits upward lift to about 0.2 mm, independently of the cross opening.
dome=sphere(3.4).intersect(slab(3.95,6.1))
cap=cross(16,5.5,6.2,8.0)
dpad=dome.fuse(cap,cyl(cx,cy,1.4,5.3,6.3),cyl(cx,cy,.95,2.55,4.1),sphere(.95))
for n in ['SW5','SW6','SW7','SW8']:
 x,y=coords[n];dpad=dpad.fuse(cyl(x,y,.85,3.75,6.3))
# Preserve contact clearance from the working button guides.
for n in ['SW4','SW9']:
 x,y=coords[n];dpad=dpad.cut(cyl(x,y,4.45,3.8,8.1))
dpad=dpad.cut(cyl(cx+6,cy,.45,7.7,8.2)).clean()
# Refill old cross aperture before cutting the new opening. Keep original guide areas.
patch=cross(18.0,7.5,CEILING,TOP)
for n in ['SW4','SW9']:
 x,y=coords[n];patch=patch.cut(cyl(x,y,4.5,3.8,8))
lid=lid.fuse(patch).cut(cross(17.1,6.6,5.9,8))
# Four diagonal braces connect a spherical upper bearing to solid roof corners.
socket=cyl(cx,cy,4.05,4.85,5.6).cut(sphere(3.6)).cut(cyl(cx,cy,1.85,4.7,6.2))
for angle in [45,135,225,315]:
 brace=box(cx+2.7,cx+5.7,cy-.65,cy+.65,4.95,5.6).fuse(box(cx+5.1,cx+5.9,cy-.65,cy+.65,5.45,6.1)).rotate((cx,cy,0),(cx,cy,1),angle)
 socket=socket.fuse(brace)
lid=lid.fuse(socket)
# A continuous outer band closes small residual chamfer notches at the old
# shoulder ends, outside the unchanged board supports (which end at Y=1.15).
base=base.fuse(box(48,69.7,1.25,2.5,BOTTOM,4.4)).clean()
lid=lid.fuse(box(48,69.7,1.25,2.5,4.4,TOP)).clean()
parts={'base':base,'lid':lid,'dpad':dpad,'select':source['select'],'back':source['back'],'usb_c_collar':source['usb_c_collar']}
# Optional matching covers; reduced cavity height. Keep the proven detent locations.
for end,x0,x1 in [('c',-14.557,5.2),('a',69.5,91.55)]:
 z=(BOTTOM+TOP)/2;inner=rounded_x(x0+1.3 if end=='c' else x0-1,x1+1 if end=='c' else x1-1.3,-10.6,z,26.6,TOP-BOTTOM+.4,.5)
 outer=rounded_x(x0,x1,-10.6,z,29.2,TOP-BOTTOM+3.,1.8)
 cover=outer.cut(inner)
 seat=-2.65 if end=='c' else 74.95;start=seat-1.6 if end=='c' else seat+1.6
 for wall_z,tip_z in [(TOP+.25,TOP-1.15),(BOTTOM-.25,BOTTOM+1.)]:
  stop=cq.Workplane('XZ',origin=(0,-3,0)).polyline([(start,wall_z),(seat,tip_z),(seat,wall_z)]).close().extrude(16).val()
  cover=cover.fuse(stop)
 # Original F detents are retained on the base, centred at z=-1.
 for y in [-23.95,2.75]:
  ra,rb=(1,3) if end=='c' else (70.2,72.2)
  rib=cq.Workplane('YZ',origin=(ra+.65,y,-1)).circle(.65).extrude(rb-ra-1.3).val()
  for x in [ra+.65,rb-.65]:rib=rib.fuse(cq.Workplane('XY').sphere(.65).translate((x,y,-1)).val())
  cover=cover.fuse(rib)
 # Matching TPU covers retain the original base detent positions.
 parts['usb_'+end+'_end_cap']=cover.clean()
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
 for angle in [-5,-4,-3,-2,-1,0,1,2,3,4,5]:
  t=dpad.rotate((cx,cy,2.55),(cx+axis[0],cy+axis[1],2.55),angle)
  for n in ['lid','base','select','back']:check('Dpad tilt '+str(axis)+' '+str(angle)+' / '+n,t,parts[n])
for n in ['select','back']:
 for dz in [0,-.2,-.4,-.6]:check(n+' travel '+str(dz),parts[n].translate((0,0,dz)),lid)
# Positive capture: a displaced cap must meet the guide before escaping.
retention=[]
for shift in [(0,0,.4),(.6,0,0),(-.6,0,0),(0,.6,0),(0,-.6,0)]:
 v=overlap(dpad.translate(shift),lid);retention.append({'shift_mm':shift,'interference_mm3':v})
 if v<1e-4:fail.append({'retention_missing':shift})
print('Movement checks',json.dumps(fail),flush=True)
if fail: raise RuntimeError('Fix movement before electronics validation')
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
check('socket insertion envelope',base,box(51.2,68,-14.8,.2,-2.15,16))
for dx in [-.5,.5]:
 if sum(overlap(parts['usb_c_collar'].translate((dx,0,0)),parts[n]) for n in ['base','lid'])<1e-4:fail.append({'collar_capture_missing':dx})
report={'revision':'I','body_mm':[77.3,26.2,TOP-BOTTOM],'previous_F_body_mm':[77.3,27.6,12.1],
 'overall_height_with_buttons_mm':8.85-BOTTOM,'dpad_pivot':[cx,cy,2.55],
 'spherical_radial_clearance_mm':.2,'dpad_opening_side_gap_mm':.55,
 'usb_a_opening_height_mm':6.7,'previous_union_opening_height_mm':8.3,
 'floor_top_z_mm':FLOOR,'floor_thickness_mm':1.2,'checks':checks,'retention':retention,'failures':fail,
 'limitations':['Physical print fit and actuation pending.','Display flex reserve reduced to 2.4 mm below PCB; dry-fit folded flex before snapping shut.','Library geometry is not measured hardware.','D-pad operational clearance checked to 5 degrees; switch compression is not simulated.']}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('FAILURES',json.dumps(fail,indent=2),flush=True)
if fail:raise RuntimeError('Review failures before exporting STLs')
for n,s in parts.items():
 if n in ['select','back','usb_c_collar']:
  shutil.copyfile(F/(n+'.stl'),O/(n+'.stl'));continue
 p=s
 if n in ['lid','dpad']:p=p.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':p=p.rotate((0,0,0),(0,1,0),-90)
 elif n=='usb_a_end_cap':p=p.rotate((0,0,0),(0,1,0),90)
 b=p.BoundingBox();p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(O/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(str(O/(n+'.stl')),force='mesh');mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 assert mesh.is_watertight and len(mesh.split())==1,n
 mesh.export(str(O/(n+'.stl')))
 report.setdefault('meshes',{})[n]={'watertight':True,'single_body':True,'size_mm':mesh.extents.tolist()}
a=cq.Assembly(name='Fuse_Vault_I')
for n,s in parts.items():a.add(s,name=n,color=cq.Color(*((.9,.62,.2) if n in ['dpad','select','back'] else (.55,.64,.73))))
a.export(str(O/'case-assembly.step'))
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(hashlib.sha256((F/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
print('Validated and exported I',flush=True)
