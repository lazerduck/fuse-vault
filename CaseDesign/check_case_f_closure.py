"""Supplemental insertion and closure checks for F. No CAD edits."""
from pathlib import Path
import json
import cadquery as cq
from build_case_e import snap_geometry
import sys
R=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent/'prototype-f'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def overlap(a,b):
 x,y=a.BoundingBox(),b.BoundingBox()
 if x.xmax<=y.xmin+1e-6 or y.xmax<=x.xmin+1e-6 or x.ymax<=y.ymin+1e-6 or y.ymax<=x.ymin+1e-6 or x.zmax<=y.zmin+1e-6 or y.zmax<=x.zmin+1e-6:return 0.
 return a.intersect(b).Volume()
p={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar']}
posts=[]
for x0,x1 in [(6.5,10),(70,73)]:
 for lower in [False,True]:
  post,_,_=snap_geometry(x0,x1,.25,lower);region=box(x0-.01,x1+.01,1.35,3,4.4,6.1)
  if lower:region=region.mirror('XZ',(0,-10.6,0))
  posts.append(post.intersect(region))
beads=cq.Compound.makeCompound(posts);fixed=p['base'].cut(beads)
checks=[];fail=[]
for angle in [0,.5,1,2,5,10,15]:
 for n in ['lid','dpad','select','back']:
  t=p[n].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
  for on,o in [('base excluding elastic snap beads',fixed),('collar',p['usb_c_collar'])]:
   v=overlap(t,o);r={'part':n,'closure_angle':angle,'obstacle':on,'overlap_mm3':v};checks.append(r)
   if v>1e-4:fail.append(r)
report={'checks':checks,'failures':fail,'limitations':['Sampled rigid closure trajectory only; snap elasticity and loose controls need physical assembly testing.']}
(R/'closure-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(fail,indent=2));assert not fail
