"""L: actuator-centred D-pad, raised screen, eased shell and stronger snap beads.
Build with the existing CadQuery 2.8 environment. K STEP exports are read-only inputs.
"""
from pathlib import Path
import json, hashlib, itertools, shutil, zipfile
import cadquery as cq
import trimesh
from build_case_e import snap_geometry
R=Path(__file__).resolve().parent; O=R/'prototype-l'; O.mkdir(exist_ok=True); K=R/'prototype-k'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def bounds(s):
 b=s.BoundingBox(); return [b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
def overlap(a,b):
 aa,bb=bounds(a),bounds(b)
 if any(aa[i+1]<=bb[i]+1e-6 or bb[i+1]<=aa[i]+1e-6 for i in (0,2,4)):return 0.
 return max(0.,a.intersect(b).Volume())
names=['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']
hashes={n:hashlib.sha256((K/(n+'.step')).read_bytes()).hexdigest() for n in names}
p={n:cq.importers.importStep(str(K/(n+'.step'))).val() for n in names}
coords={c['ref']:c['origin_mm'] for c in json.loads((R/'pcb-print-model/measurements.json').read_text())['components'] if c.get('origin_mm')}
# Centres measured from each library switch's upper actuator section, Z=3.45..3.57.
# These are model evidence, not measurements of the soldered hardware.
tips={'SW5':(58.801117602235,-5.0522844528395),'SW6':(52.73283111739251,-10.668021336043),'SW7':(59.055118110236,-16.2283068048835),'SW8':(64.4884033250765,-10.287020574041)}
for n,(x,y) in tips.items():
 ox,oy=coords[n]
 # Remove only the old projecting pad below the flange; use a narrower nose
 # and a tapered shoulder to keep the pad away from the fixed switch housing.
 p['dpad']=p['dpad'].cut(cyl(ox,oy,1.601,3.6,4.149))
 nose=cyl(x,y,1.1,3.65,3.90)
 shoulder=cq.Solid.makeCone(1.1,1.6,.3,cq.Vector(x,y,3.90))
 p['dpad']=p['dpad'].fuse(nose,shoulder)
# Relief at retaining corners: keep upper capture lands but give rocker more tilt.
p['lid']=p['lid'].cut(box(54.4,63.1,-14.8,-6.0,5.69,5.85))
# Screen platform leaves the flex end open. Nominal face: 1.6+2.4+1.5=5.5,
# rising to 5.8 with 1.6 mm module and 0.2 mm total adhesive allowance.
p['screen_shim']=box(19.1,44.5,-18.,-5.5,1.6,4.0)
raised=box(16.85146,45.25146,-18.74208,-4.74208,4.0,5.95)
p['lid']=p['lid'].cut(raised)
# Improve sight lines with a straight 45-degree flare above the existing aperture.
window=cq.Workplane('XY').workplane(offset=6.4).center(32.45146,-11.74208).rect(22.5,11.6).workplane(offset=.9).rect(24.3,13.4).loft().val()
p['lid']=p['lid'].cut(window)
# Increase positive engagement by .10 mm; keep existing posts and alignment datums.
beads=[]
for x0,x1 in [(6.5,10),(70,73)]:
 for lower in [False,True]:
  old,_,_=snap_geometry(x0,x1,.25,lower)
  new,pocket,_=snap_geometry(x0,x1,.35,lower)
  p['base']=p['base'].cut(old).fuse(new);p['lid']=p['lid'].cut(pocket)
  region=box(x0-.01,x1+.01,1.35,3,4.4,6.1)
  if lower:region=region.mirror('XZ',(0,-10.6,0))
  beads.append(new.intersect(region))
# User feedback: K cap detents were too weak. Broader rounded beads use the
# existing sleeve wall's flex: .85 radius gives .40 nominal interference/side.
# Matching 1.0 mm-radius pockets give .15 mm radial seating clearance.
for end in ['c','a']:
 for yy in [-24.15,2.95]:
  bead=cq.Workplane('XY').sphere(.85).translate((.9,yy,1.575)).val()
  pocket=cq.Workplane('XY').sphere(1.0).translate((.9,yy,1.575)).val()
  if end=='a':
   bead=bead.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
   pocket=pocket.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
  p['usb_'+end+'_end_cap']=p['usb_'+end+'_end_cap'].fuse(bead)
  p['base']=p['base'].cut(pocket)
# Intersect with an eased outer envelope: robust across the inherited face splits.
# This only removes material outside the existing cavity/closure geometry.
eased=[]
envelope=box(-2.5,74.8,-23.7,3.9,-4.1,7.25)
envelope=cq.Workplane(obj=envelope).edges('|Z').fillet(.8).faces('>Z or <Z').edges().chamfer(.55).val()
for name in ['base','lid']:
 p[name]=p[name].intersect(envelope)
 # Main upper-side edge is inset from the SD shoulder. Cut its exposed edge only.
 z=-4.1 if name=='base' else 7.25
 points=[(2.5,z),(1.95,z),(2.5,z+(.55 if name=='base' else -.55))]
 for x0,x1 in [(-2.5,49.3),(69.5,74.8)]:
  wedge=cq.Workplane('YZ',origin=(x0,0,0)).polyline(points).close().extrude(x1-x0).val()
  p[name]=p[name].cut(wedge)
 # Two projecting SD corners, relieved through the full shell height.
 for x,sign in [(49.3,1),(69.5,-1)]:
  wedge=cq.Workplane('XY').polyline([(x,3.9),(x+sign*.5,3.9),(x,3.4)]).close().extrude(20).translate((0,0,-8)).val()
  p[name]=p[name].cut(wedge)
 eased.append({'part':name,'outer_vertical_radius_mm':.8,'outer_face_chamfer_mm':.55,'SD_corner_chamfer_mm':.5})
p={n:s.clean() for n,s in p.items()}
for n,s in p.items():
 assert s.isValid() and len(s.Solids())==1,(n,'invalid solid')
 cq.exporters.export(s,str(O/(n+'.step')))
print('Geometry built',flush=True)
checks=[];fail=[]
def check(label,a,b):
 v=overlap(a,b);item={'check':label,'overlap_mm3':v};checks.append(item)
 if v>1e-4:fail.append(item)
for (n,s),(m,t) in itertools.combinations(p.items(),2):check(n+' / '+m,s,t)
for axis in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for angle in [-4,-3,-2,-1,0,1,2,3,4]:
  t=p['dpad'].rotate((58.77,-10.38,2.55),(58.77+axis[0],-10.38+axis[1],2.55),angle)
  for n in ['lid','base','select','back']:check('rock '+str(axis)+' '+str(angle)+' / '+n,t,p[n])
for n in ['select','back']:
 for dz in [0,-.2,-.4,-.6]:check(n+' stroke '+str(dz),p[n].translate((0,0,dz)),p['lid'])
for n in ['dpad','select','back']:
 for dz in [-12,-8,-5,-3,-2,-1,-.5,-.2,0]:check(n+' insertion '+str(dz),p[n].translate((0,0,dz)),p['lid'])
retention=[]
for shift in [(0,0,.6),(.6,0,0),(-.6,0,0),(0,.6,0),(0,-.6,0)]:
 v=overlap(p['dpad'].translate(shift),p['lid']);retention.append({'shift':shift,'volume_mm3':v})
 if v<1e-4:fail.append({'capture_missing':shift})
fixed=p['base'].cut(cq.Compound.makeCompound(beads))
for angle in [0,.5,1,2,5,10,15]:
 for n in ['lid','dpad','select','back']:
  t=p[n].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
  for on,o in [('base excluding snap beads',fixed),('collar',p['usb_c_collar']),('screen shim',p['screen_shim'])]:check('closure '+n+' '+str(angle)+' / '+on,t,o)
print('Movement failures',fail,flush=True)
report={'revision':'L','checks':checks,'failures':fail,'source_step_sha256':hashes,'actuator_centres_mm':tips,'contact_face_diameter_mm':2.2,'contact_tip_z_mm':3.65,'screen_shim_mm':2.4,'screen_max_face_with_adhesive_mm':5.8,'screen_reserved_ceiling_mm':5.95,'snap_engagement_mm':.35,'cap_bead_radius_mm':.85,'cap_pocket_radius_mm':1.0,'cap_nominal_wall_flex_per_side_mm':.4,'eased_edges':eased,'retention':retention,'limitations':['Actuator centres and travel checks use library geometry, not measured switch force or solder placement.','Physical print, screen adhesive/flex routing, snap force and D-pad feel require assembly testing.','The switches must not be pressed at rest; do not force closure or use the display as a spacer.']}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
if fail:raise RuntimeError('Movement failures')
board_path=R/'pcb-print-model/source-board.step';cache=Path('/tmp/fuse-vault-case-'+hashlib.sha256(board_path.read_bytes()).hexdigest()[:12]+'.brep')
reference=cq.Shape.importBrep(str(cache)) if cache.exists() else cq.importers.importStep(str(board_path)).val()
for n,s in p.items():
 print('Electronics',n,flush=True)
 for i,e in enumerate(reference.Solids()):check(n+' / electronics '+str(i),s,e)
 # Explicit screen envelope includes drawing tolerance and adhesive above shim.
 check(n+' / screen',s,box(16.95146,45.15146,-18.64208,-4.84208,4.00001,5.8))
for n in ['base','lid']:check(n+' / installed SD',p[n],box(50.7,68.1,-15,2.5,-2.7,0))
check('board loading',p['base'],box(-.127,73.208,-21.237,0,.001,16))
check('SD loading',p['base'],box(50.7,68.1,-15,2.5,-2.7,16))
# Closed snaps must resist a small straight lift in the CAD model.
for x0,x1 in [(6.5,10),(70,73)]:
 for lower in [False,True]:
  post,_,_=snap_geometry(x0,x1,.35,lower)
  v=overlap(post,p['lid'].translate((0,0,.4)))
  if v<1e-4:fail.append({'snap_capture_missing':[x0,lower]})
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('Final failures',fail,flush=True)
if fail:raise RuntimeError('Electronics failures')
for n,s in p.items():
 t=s
 if n in ['lid','dpad','select','back']:t=t.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':t=t.rotate((0,0,0),(0,1,0),-90)
 elif n in ['usb_a_end_cap','usb_c_collar']:t=t.rotate((0,0,0),(0,1,0),90)
 b=t.BoundingBox();t=t.translate((-b.xmin,-b.ymin,-b.zmin));path=O/(n+'.stl')
 cq.exporters.export(t,str(path),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(str(path),force='mesh');mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 assert mesh.is_watertight and len(mesh.split())==1,n
 mesh.export(str(path));report.setdefault('meshes',{})[n]={'watertight':True,'single_body':True,'size_mm':mesh.extents.tolist()}
shutil.copyfile(O/'usb_c_end_cap.stl',O/'universal_end_cap.stl')
a=cq.Assembly(name='Fuse_Vault_L')
for n,s in p.items():a.add(s,name=n,color=cq.Color(*((.9,.62,.2) if n in ['dpad','select','back','screen_shim'] else (.55,.64,.73))))
a.export(str(O/'case-assembly.step'))
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(hashlib.sha256((K/(n+'.step')).read_bytes()).hexdigest()==h for n,h in hashes.items())
print('Validated and exported L',flush=True)
