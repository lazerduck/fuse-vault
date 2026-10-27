"""K: installed-card space, PLA sleeve symmetry, button height and broad contact pads."""
from pathlib import Path
import json, math
import cadquery as cq
R=Path(__file__).resolve().parent;O=R/'prototype-k'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def overlap(a,b):
 x,y=a.BoundingBox(),b.BoundingBox()
 if x.xmax<=y.xmin+1e-6 or y.xmax<=x.xmin+1e-6 or x.ymax<=y.ymin+1e-6 or y.ymax<=x.ymin+1e-6 or x.zmax<=y.zmin+1e-6 or y.zmax<=x.zmin+1e-6:return 0.
 return max(0,a.intersect(b).Volume())
p={n:cq.importers.importStep(str(O/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']}
checks=[];fail=[]
def check(label,v):
 checks.append({'check':label,'volume_mm3':v})
 if v>1e-4:fail.append(checks[-1])
def same(label,a,b):check(label,a.cut(b).Volume()+b.cut(a).Volume())
# Unlike the J check, include the card extension and the deeper clearance from F.
reserve=box(50.7,68.1,-15,2.5,-2.7,0)
for n in ['base','lid']:check('installed card/socket reserve / '+n,overlap(reserve,p[n]))
check('card/socket loading from above',overlap(box(50.7,68.1,-15,2.5,-2.7,16),p['base']))
check('closed outer SD wall',box(50.7,68.1,2.6,3.8,-2.7,7.1).cut(p['base']).cut(p['lid']).Volume())
check('continuous SD floor',box(50.7,68.1,-15,2.5,-4.0,-3.0).cut(p['base']).Volume())
# Card shoulder joins the main body at both sides, including below the PCB.
for x0,x1 in [(49.5,50.6),(68.2,69.4)]:
 check('closed SD end wall '+str(x0),box(x0,x1,1.3,3.8,-2.7,4.3).cut(p['base']).Volume())
for n in ['select','back']:
 original=cq.importers.importStep(str(R/'prototype-f'/(n+'.step'))).val()
 same(n+' only exposed top trimmed',p[n],original.intersect(box(-30,100,-40,20,-10,8.3)))
 assert abs((p[n].BoundingBox().zmax-7.25)-(original.BoundingBox().zmax-7.8))<1e-5
# Identical cap geometry at either end, also invariant when rolled upside down.
rot=lambda s:s.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
roll=lambda s:s.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)
same('same cap either end',rot(p['usb_c_end_cap']),p['usb_a_end_cap'])
for end in ['c','a']:same(end+' cap upside-down symmetry',p['usb_'+end+'_end_cap'],roll(p['usb_'+end+'_end_cap']))
elastic=[]
for end in ['c','a']:
 cap=p['usb_'+end+'_end_cap'];hard=cap
 for yy in [-24.15,2.95]:
  bump=cq.Workplane('XY').sphere(.551).translate((.9,yy,1.575)).val()
  hard=hard.cut(rot(bump) if end=='a' else bump)
 for d in [0,.2,.5,1,2,3,4,5,6,8,15,25]:
  shift=(-d if end=='c' else d,0,0)
  for n in ['base','lid','usb_c_collar']:check(end+' cap rigid removal '+str(d)+' / '+n,overlap(hard.translate(shift),p[n]))
  elastic.append({'end':end,'distance_mm':d,'detent_interference_mm3':sum(overlap(cap.translate(shift),p[n]) for n in ['base','lid'])})
# Both insertion directions are covered by this same reversible path.
for n in ['dpad','select','back']:
 for dz in [-12,-8,-5,-3,-2,-1,-.5,-.2,0]:check(n+' underside insertion '+str(dz),overlap(p[n].translate((0,0,dz)),p['lid']))
# Inspect the contact face at each switch and a conservative housing-height plane.
coords={c['ref']:c['origin_mm'] for c in json.loads((R/'pcb-print-model/measurements.json').read_text())['components'] if c.get('origin_mm')}
pads=[]
for n in ['SW5','SW6','SW7','SW8']:
 x,y=coords[n];expected=cq.Workplane('XY').center(x,y).circle(1.6).extrude(.01).translate((0,0,3.65)).val()
 check(n+' complete 3.2 mm contact face',expected.cut(p['dpad']).Volume())
 minimum=100
 for axis in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
  for angle in [-3,-2,-1,0,1,2,3]:
   q=expected.rotate((58.77,-10.38,2.55),(58.77+axis[0],-10.38+axis[1],2.55),angle)
   minimum=min(minimum,q.BoundingBox().zmin)
 if minimum<3.2:fail.append({'contact_housing_plane_interference':n,'min_z_mm':minimum})
 pads.append({'switch':n,'contact_diameter_mm':3.2,'minimum_contact_face_z_at_sampled_tilts_mm':minimum,'nominal_housing_top_z_mm':3.0,'conservative_check_plane_z_mm':3.2})
report={'checks':checks,'pad_checks':pads,'detent_motion':elastic,'failures':fail,'limitations':['PLA detent fit and durability require a print; nominal wall deflection is .10 mm per side.','Restored F installed-card allowance is not a new physical measurement.','Housing-plane checks are nominal geometric checks, not switch force/travel simulation.']}
(O/'detail-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(fail,indent=2));assert not fail
