"""Independent source-actuator alignment and moving-housing checks for revision L."""
from pathlib import Path
import cadquery as cq
import json,hashlib
R=Path(__file__).resolve().parent;O=R/'prototype-l'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def overlap(a,b):
 x,y=a.BoundingBox(),b.BoundingBox()
 if x.xmax<=y.xmin+1e-6 or y.xmax<=x.xmin+1e-6 or x.ymax<=y.ymin+1e-6 or y.ymax<=x.ymin+1e-6 or x.zmax<=y.zmin+1e-6 or y.zmax<=x.zmin+1e-6:return 0.
 return max(0,a.intersect(b).Volume())
p={n:cq.importers.importStep(str(O/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','screen_shim','usb_c_collar','usb_c_end_cap','usb_a_end_cap']}
source=R/'pcb-print-model/source-board.step';cache=Path('/tmp/fuse-vault-case-'+hashlib.sha256(source.read_bytes()).hexdigest()[:12]+'.brep')
reference=cq.Shape.importBrep(str(cache)) if cache.exists() else cq.importers.importStep(str(source)).val()
coords={c['ref']:c['origin_mm'] for c in json.loads((R/'pcb-print-model/measurements.json').read_text())['components'] if c.get('origin_mm')}
checks=[];fail=[]
def check(label,v):
 row={'check':label,'volume_mm3':v};checks.append(row)
 if v>1e-4:fail.append(row)
actuators=[]
for n in ['SW5','SW6','SW7','SW8']:
 x,y=coords[n]
 candidates=[s for s in reference.Solids() if abs(s.BoundingBox().center.x-x)<2 and abs(s.BoundingBox().center.y-y)<2 and s.BoundingBox().zmax>3.1]
 assert len(candidates)==1,(n,len(candidates))
 switch=candidates[0];top=switch.intersect(box(x-4,x+4,y-4,y+4,3.45,3.57));b=top.BoundingBox();tx,ty=b.center.x,b.center.y
 face=cq.Workplane('XY').center(tx,ty).circle(1.1).extrude(.01).translate((0,0,3.65)).val()
 check(n+' complete contact face at source actuator centre',face.cut(p['dpad']).Volume())
 # Source sections transition from the broad housing at Z=3.0 to only the
 # narrow actuator by Z=3.05. A Z=3.2 cut wrongly classified moving actuator
 # material as housing. Assert that distinction for every switch, and record it.
 sections=[]
 for z in [3.0,3.05,3.2]:
  section=switch.intersect(box(x-4,x+4,y-4,y+4,z-.0025,z+.0025));sb=section.BoundingBox()
  sections.append({'z_mm':z,'xy_extent_mm':[sb.xlen,sb.ylen],'area_mm2':section.Volume()/.005})
 assert max(sections[0]['xy_extent_mm'])>2.7,n
 assert max(sections[1]['xy_extent_mm'])<2.15,n
 housing=switch.intersect(box(x-4,x+4,y-4,y+4,0,3.05))
 for axis in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
  for angle in [-4,-3,-2,-1,0,1,2,3,4]:
   t=p['dpad'].rotate((58.77,-10.38,2.55),(58.77+axis[0],-10.38+axis[1],2.55),angle)
   check(n+' fixed housing rock '+str(axis)+' '+str(angle),overlap(t,housing))
 actuators.append({'switch':n,'placement_origin_mm':[x,y],'source_actuator_centre_mm':[tx,ty],'correction_mm':[tx-x,ty-y],'nominal_rest_gap_mm':3.65-b.zmax,'source_sections':sections,'fixed_housing_check_ceiling_mm':3.05})
for n in ['select','back','usb_c_collar']:
 k=cq.importers.importStep(str(R/'prototype-k'/(n+'.step'))).val()
 check(n+' preserved from K',p[n].cut(k).Volume()+k.cut(p[n]).Volume())
# New screen must clear the closing lid, not just the final assembled position.
screen=box(16.95146,45.15146,-18.64208,-4.84208,4.00001,5.8)
for angle in [0,.5,1,2,5,10,15]:
 t=p['lid'].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
 check('raised display / closing lid '+str(angle),overlap(t,screen))
# Hard cap removal excludes only the existing intentional flexible bump geometry.
rot=lambda s:s.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
check('identical caps at both ends',rot(p['usb_c_end_cap']).cut(p['usb_a_end_cap']).Volume()+p['usb_a_end_cap'].cut(rot(p['usb_c_end_cap'])).Volume())
roll=lambda s:s.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)
cap_flex=[]
for end in ['c','a']:
 cap=p['usb_'+end+'_end_cap']
 check(end+' cap 180 degree roll symmetry',cap.cut(roll(cap)).Volume()+roll(cap).cut(cap).Volume())
 hard=p['usb_'+end+'_end_cap']
 for yy in [-24.15,2.95]:
  bump=cq.Workplane('XY').sphere(.851).translate((.9,yy,1.575)).val();hard=hard.cut(rot(bump) if end=='a' else bump)
 for d in [0,.2,.5,1,2,3,4,5,6,8,15,25]:
  cap_flex.append({'end':end,'travel_mm':d,'intentional_bead_interference_mm3':sum(overlap(cap.translate((-d if end=='c' else d,0,0)),p[n]) for n in ['base','lid'])})
  for n in ['base','lid','usb_c_collar']:check(end+' cap removal '+str(d)+' / '+n,overlap(hard.translate((-d if end=='c' else d,0,0)),p[n]))
report={'checks':checks,'actuator_alignment':actuators,'cap_flex_samples':cap_flex,'cap_nominal_flex_per_side_mm':.4,'failures':fail,'limitations':['Source section checks distinguish the broad housing below Z=3.05 from the moving actuator above it; actual switch force and compressible actuator travel are not simulated.','Source switch models may differ from soldered hardware.','Cap detent elasticity and snap retention force need physical testing.']}
(O/'detail-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(fail,indent=2));assert not fail
