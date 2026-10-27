"""Final exported-I checks: sealed SD wall, unchanged buttons and cap removal."""
from pathlib import Path
import hashlib,json
import cadquery as cq
R=Path(__file__).resolve().parent;O=R/'prototype-i'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def overlap(a,b):
 x,y=a.BoundingBox(),b.BoundingBox()
 if x.xmax<=y.xmin+1e-6 or y.xmax<=x.xmin+1e-6 or x.ymax<=y.ymin+1e-6 or y.ymax<=x.ymin+1e-6 or x.zmax<=y.zmin+1e-6 or y.zmax<=x.zmin+1e-6:return 0.
 return max(0,a.intersect(b).Volume())
p={n:cq.importers.importStep(str(O/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']}
wall=box(51.1,68.1,1.3,2.0,-2.4,7.0)
missing=wall.cut(p['base']).cut(p['lid']).Volume()
assert missing<1e-5,('SD side wall has a hole',missing)
unchanged={}
for n in ['select','back','usb_c_collar']:
 a=(R/'prototype-f'/(n+'.stl')).read_bytes();b=(O/(n+'.stl')).read_bytes()
 assert a==b,n
 unchanged[n]=hashlib.sha256(b).hexdigest()
# Motion of elastic detent ribs is intentional. Check the hard cap shell and stops
# against case/collar across axial removal; strip only the narrow rib regions.
checks=[];allowed=[]
for end in ['c','a']:
 cap=p['usb_'+end+'_end_cap'];hard=cap
 x0,x1=(.9,3.1) if end=='c' else (70.1,72.3)
 for y0,y1 in [(-24.,-23.2),(2.,2.8)]:hard=hard.cut(box(x0,x1,y0,y1,-1.7,-.3))
 for d in [0,.5,1,2,4,8,15,25]:
  shift=(-d if end=='c' else d,0,0)
  for n in ['base','lid','usb_c_collar']:
   v=overlap(hard.translate(shift),p[n]);checks.append({'cap':end,'removal_mm':d,'obstacle':n,'rigid_overlap_mm3':v})
   assert v<1e-4,checks[-1]
  allowed.append({'cap':end,'removal_mm':d,'elastic_overlap_mm3':sum(overlap(cap.translate(shift),p[n]) for n in ['base','lid'])})
r={'SD_wall_missing_volume_mm3':missing,'unchanged_STL_sha256':unchanged,'cap_removal':checks,'elastic_detent_motion':allowed,'failures':[]}
(O/'detail-validation.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS: SD wall closed; buttons/collar identical; rigid cap removal clear')
