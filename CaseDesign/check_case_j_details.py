"""Assembly, interchangeable flush caps and preserved controls for revision J."""
from pathlib import Path
import json
import cadquery as cq
R=Path(__file__).resolve().parent;O=R/'prototype-j'
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
check('continuous SD wall',box(51.1,68.1,1.3,2.,-2.4,7).cut(p['base']).cut(p['lid']).Volume())
rot=lambda s:s.rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
a=rot(p['usb_c_end_cap']);b=p['usb_a_end_cap']
check('same cap fits either end: symmetric difference',a.cut(b).Volume()+b.cut(a).Volume())
# Isolate only the intentional elastic bumps. The rest must slide freely.
elastic=[]
for end in ['c','a']:
 cap=p['usb_'+end+'_end_cap'];hard=cap
 for yy in [-22.6,1.4]:
  bump=cq.Workplane('XY').sphere(.351).translate((.4,yy+(-.30 if yy<0 else .30),-1)).val()
  hard=hard.cut(rot(bump) if end=='a' else bump)
 for d in [0,.2,.5,1,2,3,4,5,8,15,25]:
  shift=(-d if end=='c' else d,0,0)
  for n in ['base','lid','usb_c_collar']:
   check(end+' cap rigid removal '+str(d)+' / '+n,overlap(hard.translate(shift),p[n]))
  elastic.append({'end':end,'distance_mm':d,'elastic_interference_mm3':sum(overlap(cap.translate(shift),p[n]) for n in ['base','lid'])})
# Detached lid upside down: insert controls through underside, never through roof.
for name in ['dpad','select','back']:
 for dz in [-12,-8,-5,-3,-2,-1,-.5,-.2,0]:check(name+' vertical insertion '+str(dz),overlap(p[name].translate((0,0,dz)),p['lid']))
for name in ['select','back','usb_c_collar']:
 if (O/(name+'.stl')).exists() and (O/(name+'.stl')).read_bytes()!=(R/'prototype-f'/(name+'.stl')).read_bytes():fail.append({'changed_part':name})
report={'checks':checks,'elastic_detent_motion':elastic,'failures':fail,'limitations':['TPU retention force and repeated wear need a printed trial.','Contact positions use PCB library geometry; actual switch tolerance and feel are unmeasured.']}
(O/'detail-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(fail,indent=2));assert not fail
