"""Prototype G: lower roof, flush reversible TPU caps, stronger snaps, AMS lid.
Run with CadQuery 2.8 / trimesh. F and earlier designs are read-only inputs.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import json, hashlib, itertools, zipfile
import cadquery as cq
import trimesh
from build_case_e import snap_geometry, rounded_x
R=Path(__file__).resolve().parent; O=R/'prototype-g'; O.mkdir(exist_ok=True)
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
_bounds_cache={}
def bounds(s):
 key=id(s)
 if key not in _bounds_cache:
  b=s.BoundingBox();_bounds_cache[key]=(s,[b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax])
 return _bounds_cache[key][1]
def overlap(a,b):
 aa,bb=bounds(a),bounds(b)
 if any(aa[i+1]<=bb[i]+1e-6 or bb[i+1]<=aa[i]+1e-6 for i in (0,2,4)):return 0.
 return a.intersect(b).Volume()
def slab(z0,z1):return box(-30,110,-35,15,z0,z1)
source={n:cq.importers.importStep(str(R.parent/'prototype-f'/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar']}
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (R.parent/'prototype-f').iterdir() if p.is_file()}
base=source['base']; old=source['lid']
# Keep PCB datums and the lower guides fixed; rebuild only the roof one mm lower.
lid=old.intersect(slab(-10,5.55)).fuse(old.intersect(slab(6.55,8)).translate((0,0,-1))).clean()
# The collar must not touch the lowered roof, including on the hooked closure path.
collar=source['usb_c_collar']
for angle in [0,5,10,15]:
 relief=rounded_x(-5,-.35,-10.668,1.85,14.9,7.9,3.9).rotate((-.5,0,3.2),(-.5,1,3.2),angle)
 lid=lid.cut(relief)
# A modest +0.10 mm increase, with matching recesses (not a seated interference).
for x0,x1 in [(6.5,10),(70,73)]:
 for lower in [False,True]:
  post,pocket,_=snap_geometry(x0,x1,.35,lower)
  base=base.fuse(post);lid=lid.cut(pocket)
# Lower control skirts 0.4 mm but preserve tips and the PCB-resting pivot.
controls={}
for n in ['dpad','select','back']:
 s=source[n]
 s=s.intersect(slab(-5,4.5)).fuse(s.intersect(slab(4.0,12)).translate((0,0,-.4)))
 s=s.intersect(slab(-5,7.95 if n=='dpad' else 7.85)).clean()
 controls[n]=s
# Relieve the hidden shoulders under the lowered roof; preserve captive flanges.
for n in ['select','back']:
 bb=source[n].BoundingBox(); x=(bb.xmin+bb.xmax)/2;y=(bb.ymin+bb.ymax)/2
 lid=lid.cut(cq.Solid.makeCone(3.25,2.5,.7,cq.Vector(x,y,5.4)))
def crosspts(span,width):
 a=span/2;b=width/2
 return [(-b,-a),(b,-a),(b,-b),(a,-b),(a,b),(b,b),(b,a),(-b,a),(-b,b),(-a,b),(-a,-b),(-b,-b)]
relief=cq.Workplane('XY').workplane(offset=5.4).polyline(crosspts(18.3,7.8)).close().workplane(offset=.7).polyline(crosspts(17.5,7.0)).close().loft().translate((58.77,-10.38,0)).val()
lid=lid.cut(relief)
# Extend the old display clearance through the raised module envelope; this
# removes a small corner of the adjacent guide outside its working bore.
lid=lid.cut(box(16.85146,45.25146,-18.74208,-4.74208,1.85,5.45))
# Recessed side/floor shoulders. The connector opening intentionally interrupts
# the neck roof, rather than leaving a fragile film over the USB-C collar.
neck=rounded_x(-20,100,-10.6,1.25,23.9,8.8,.65)
for x0,x1 in [(-2.6,1.6),(73.25,74.9)]:
 region=box(x0,x1,-30,10,-6,10)
 for name in ['base','lid']:
  s=base if name=='base' else lid
  s=s.cut(region.cut(neck))
  if name=='base':
   floor=box(max(x0,-2.5),min(x1,74.8),-21.85,.65,-3.15,-1.95).intersect(neck)
   s=s.fuse(floor)
   # Preserve the complete open connector envelope.
   if x0>0:s=s.cut(box(68.45,95,-17.65,-4.45,-1.55,5.15))
   base=s.clean()
  else:
   s=s.cut(box(x0,x1,-18.15,-3.05,4.4,5.66))
   lid=s.clean()
# Reversible symmetric detents at mid-height, outside the electronics.
caps={};cap_hard={};ribs_all={}
for end,x0,x1,mouth,rx in [('c',-14.557,1.45,1.45,.3),('a',73.4,91.55,73.4,74.0)]:
 outer=rounded_x(x0,x1,-10.6,1.25,26.2,11.1,1.5)
 outer=cq.Workplane(obj=outer).faces('<X' if end=='c' else '>X').edges().fillet(.5).val()
 inner=rounded_x(x0+1.3 if end=='c' else x0-1,x1+1 if end=='c' else x1-1.3,-10.6,1.25,24.2,9.1,.5)
 hard=outer.cut(inner);ribs=[]
 for y in [-22.8,1.6]:
  rib=cq.Workplane('XY').sphere(.55).translate((rx,y,1.25)).val()
  ribs.append(rib)
  pocket=cq.Workplane('XY').sphere(.75).translate((rx,y,1.25)).val()
  base=base.cut(pocket);lid=lid.cut(pocket)
 caps['usb_'+end+'_end_cap']=hard.fuse(*ribs).clean();cap_hard[end]=hard;ribs_all[end]=cq.Compound.makeCompound(ribs)
# Vault icon reconstructed as clean polygons from the supplied brand reference.
# Two angular arms, an orange diamond, and restrained 45-degree accent tracks.
def poly(points,z=6.4,h=.4):return cq.Workplane('XY').polyline(points).close().extrude(h).translate((0,0,z)).val()
cx,cy=8.5,-10.6
arm=[(.65,7),(.65,4.3),(-3.6,0),(-1.55,-2.05),(-2.95,-3.45),(-5.65,-.75),(-5.65,1.25)]
a=poly([(cx+x,cy+y) for x,y in arm]);b=poly([(cx-x,cy-y) for x,y in arm]);grey=cq.Compound.makeCompound([a,b])
diamond=poly([(cx-1.65,cy),(cx,cy+1.65),(cx+1.65,cy),(cx,cy-1.65)])
traces=[poly([(19,-21.4),(36,-21.4),(38,-19.4),(37,-19.4),(35.8,-20.6),(19,-20.6)]),poly([(22,.1),(41,.1),(43,-1.9),(42,-1.9),(40.8,-.7),(22,-.7)])]
orange=cq.Compound.makeCompound([diamond,*traces])
# Clip decorative solids against the roof: never bridge an aperture or guide.
grey=grey.intersect(lid);orange=orange.intersect(lid)
lid_plain=lid
lid=lid.cut(grey).cut(orange).clean()
parts={'base':base.clean(),'lid':lid,**controls,'usb_c_collar':collar,**caps,'lid_inlay_grey':grey,'lid_inlay_orange':orange}
# Broad support under the backlight; open at the flex end, away from the cutout.
# Nominal PCB top 1.6 + shim 1.8 + module 1.5 = face 4.9 before adhesive.
shims={f'screen_shim_{h:.1f}mm':box(19.1,44.5,-18.0,-5.5,1.6,1.6+h) for h in [1.6,1.8,2.0]}
checks=[];fail=[];allowed=[]
def check(label,a,b):
 v=overlap(a,b);checks.append({'check':label,'overlap_mm3':v})
 if v>1e-4:fail.append(checks[-1])
print('Validate shapes',flush=True)
for n,s in parts.items():
 if not s.isValid() or (not n.startswith('lid_inlay') and len(s.Solids())!=1):fail.append({'shape':n,'valid':s.isValid(),'solids':len(s.Solids())})
for (n,s),(m,t) in itertools.combinations(parts.items(),2):check(n+' / '+m,s,t)
for ax in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for ang in [-6,-4,-2,0,2,4,6]:
  t=controls['dpad'].rotate((58.77,-10.38,2.55),(58.77+ax[0],-10.38+ax[1],2.55),ang)
  for n in ['lid','base','select','back']:check('Dpad tilt '+str(ax)+' '+str(ang)+' / '+n,t,parts[n])
for n in ['select','back']:
 for dz in [-.6,-.3,0]:check(n+' travel '+str(dz),controls[n].translate((0,0,dz)),lid)
print('Validate electronics',flush=True)
reference=cq.Shape.importBrep('/tmp/fuse-vault-case-d97f7ce3ed3c.brep') if Path('/tmp/fuse-vault-case-d97f7ce3ed3c.brep').exists() else cq.importers.importStep(str(R.parent/'pcb-print-model/source-board.step')).val()
electronics=reference.Solids()
for n,s in {**parts,**shims}.items():
 print('Electronics:',n,flush=True)
 for i,e in enumerate(electronics):check(n+' / electronics '+str(i),s,e)
for n,s in parts.items():
 # Worst shim and 1.6 mm module plus 0.2 mm total adhesive allowance.
 check(n+' / raised screen reserve',s,box(16.95,45.15,-18.65,-4.84,3.6,5.4))
for h,s in shims.items():
 for n in ['base','lid']:check(h+' / '+n,s,parts[n])
for n in ['base','lid']:check('SD withdrawal '+n,parts[n],box(51.15,68.05,-.5,25,-2.4,.9))
check('PCB loading',base,box(-.127,73.208,-21.237,0,.001,16))
for dx in [-.5,.5]:
 v=sum(overlap(collar.translate((dx,0,0)),parts[n]) for n in ['base','lid'])
 if v<1e-4:fail.append({'collar_capture_lost':dx})
for end in ['c','a']:
 print('Cap path:',end,flush=True)
 n='usb_'+end+'_end_cap';s=parts[n]
 flipped=s.rotate((0,-10.6,1.25),(1,-10.6,1.25),180)
 if abs(flipped.cut(s).Volume())>1e-4:fail.append({'cap_not_reversible':end})
 for d in [0,.5,1,2,4,8,15,25]:
  shift=(-d if end=='c' else d,0,0)
  for k in ['base','lid','usb_c_collar']:check(end+' cap rigid removal '+str(d)+' / '+k,cap_hard[end].translate(shift),parts[k])
  moved=s.translate(shift)
  for i,e in enumerate(electronics):check(end+' cap electronics path '+str(d)+' / '+str(i),moved,e)
  allowed.append({'cap':end,'removal_mm':d,'elastic_rib_overlap_mm3':sum(overlap(ribs_all[end].translate(shift),parts[k]) for k in ['base','lid'])})
report={'revision':'G','failures':fail,'checks':checks,'intentional_elastic_motion':allowed,'body_height_mm':11.1,'overall_control_height_mm':12.25,'snap_engagement_mm':.35,'screen_shims_mm':[1.6,1.8,2.0],'screen_module_mm':1.5,'screen_module_source':'CASE_DESIGN_BRIEF.md supplied mechanical drawing; F used obsolete 2.5 mm placeholder','limitations':['Physical fit, print tolerances, cap retention, snap force and fatigue require sample prints.','Screen installation height and glue thickness are unmeasured; dry-fit shims and leave clearance under bezel.','TPU caps use thin 1 mm walls; material hardness requires testing.']}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('FAILURES',json.dumps(fail,indent=2),flush=True)
if fail:raise RuntimeError('Validation failed; no print exports produced')
print('Export',flush=True)
# Fit samples preserve opposing walls; cap sample uses the full C end, collar optional.
extras={**shims,'base_snap_coupon':base.intersect(box(5,13,-25,5,-5,8)),'lid_snap_coupon':lid_plain.intersect(box(5,13,-25,5,-5,8)),'base_cap_coupon':base.intersect(box(-3,5,-25,5,-5,8)),'lid_cap_coupon':lid_plain.intersect(box(-3,5,-25,5,-5,8))}
for n,s in {**parts,**extras,'lid_single_colour':lid_plain}.items():
 cq.exporters.export(s,str(O/(n+'.step')))
 if n.startswith('lid') or n in ['dpad','select','back']:p=s.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':p=s.rotate((0,0,0),(0,1,0),-90)
 elif n in ['usb_a_end_cap','usb_c_collar']:p=s.rotate((0,0,0),(0,1,0),90)
 else:p=s
 b=p.BoundingBox()
 # Shared origin for every lid colour, mandatory for multipart slicer import.
 if n in ['lid','lid_inlay_grey','lid_inlay_orange']:p=p.translate((2.5,3.9,6.8))
 else:p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(O/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(str(O/(n+'.stl')),force='mesh')
 mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 mesh.export(str(O/(n+'.stl')))
 assert mesh.is_watertight,n
 report.setdefault('exports',{})[n]={'valid':s.isValid(),'solids':len(s.Solids()),'watertight':True,'size_mm':mesh.extents.tolist()}
colors={'base':(.055,.065,.08),'lid':(.09,.10,.12),'dpad':(1,.43,.025),'select':(.65,.7,.74),'back':(.65,.7,.74),'lid_inlay_grey':(.78,.83,.86),'lid_inlay_orange':(1,.43,.025)}
a=cq.Assembly(name='Fuse_Vault_G')
for n,s in parts.items():a.add(s,name=n,color=cq.Color(*colors.get(n,(.08,.09,.1))))
a.export(str(O/'case-assembly.step'))
# A coloured STEP group retains colour names for assigning AMS filaments.
a=cq.Assembly(name='AMS_lid')
for n in ['lid','lid_inlay_grey','lid_inlay_orange']:a.add(parts[n].rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,6.8)),name=n,color=cq.Color(*colors[n]))
a.export(str(O/'lid-ams.step'))
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(hashlib.sha256((R.parent/'prototype-f'/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
print('G exported',flush=True)
