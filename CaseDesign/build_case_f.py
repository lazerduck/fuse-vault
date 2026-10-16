"""Revision F: current D-pad PCB with the physically tested captive controls.
Earlier revisions and the tested control files remain unchanged.
Run: python CaseDesign/build_case_f.py (CadQuery 2.8, trimesh, VTK).
"""
from pathlib import Path
import json,hashlib,math,itertools,shutil
import cadquery as cq
import trimesh
ROOT=Path(__file__).resolve().parent;OUT=ROOT/'prototype-f';OUT.mkdir(exist_ok=True)
E=ROOT/'prototype-e';SRC=ROOT/'pcb-print-model'
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def bounds(s):
 b=s.BoundingBox();return [b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
def overlap(a,b):
 aa,bb=bounds(a),bounds(b)
 if any(aa[i+1]<=bb[i]+1e-6 or bb[i+1]<=aa[i]+1e-6 for i in (0,2,4)):return 0.
 return a.intersect(b).Volume()
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for d in [E,ROOT/'control-lid-test'] for p in d.iterdir() if p.is_file()}
base=cq.importers.importStep(str(E/'base.step')).val();lid=cq.importers.importStep(str(E/'lid.step')).val()
original_base,original_lid=base,lid
# Reuse exactly the constructed controls in their original assembly coordinates.
control_file=ROOT/'control-lid-test/build.py'
ns={'__file__':str(control_file)}
exec(compile(control_file.read_text().split('checks=[]')[0],str(control_file),'exec'),ns)
controls={k:ns['parts'][k].translate((0,0,-2.05)) for k in ['dpad','select','back']}
# Replace the old joystick hole, leaf button and local roof, leaving the case rim.
patch=box(44.25,69.15,-20.65,-.25,3.0,9.0)
lid=lid.cut(patch).fuse(box(44.25,69.15,-20.65,-.25,6.55,7.8))
# Copy the tested guide geometry, not the test fixture's rails or USB datum stop.
for n in ['SW4','SW9']:
 x,y=ns['coords'][n]
 sleeve=cyl(x,y,3.95,4.15,6.6).cut(cyl(x,y,3.45,4.05,6.6))
 lid=lid.fuse(sleeve).cut(cyl(x,y,2.5,6.45,8.1))
lid=lid.cut(ns['cross'](17.5,7.0,8.5,10.2).translate((0,0,-2.05)))
# Preserve the previous display envelope as a provisional clearance reserve.
display=box(17.10146,45.00146,-18.49208,-4.99208,2.1,4.6)
display_reserve=box(16.85146,45.25146,-18.74208,-4.74208,1.85,4.85)
lid=lid.cut(display_reserve)
# Current microSD socket extends farther toward USB-A. Keep the base open upward
# here for loading, and expose the edge for card insertion/removal with lid closed.
sd_loading=box(51.1,68.1,-15.0,5.0,-2.45,15)
base=base.cut(sd_loading)
# Move the upper x=68 support to x=69, clear of the socket's metal tabs.
base=base.cut(box(66.8,69.2,-.7,1.4,-2.9,2.6))
lid=lid.cut(box(66.8,69.2,-.7,1.4,1.7,6.5))
base=base.fuse(box(68.4,69.6,-.55,1.15,-2.9,0))
lid=lid.fuse(box(68.4,69.6,-.55,1.15,1.8,6.65))
base=base.cut(box(68.2,69.8,-.75,1.35,1.6,4.5))
# The current AM90 library model extends below the PCB; open the port to suit it.
usb_a_clear=box(68.45,95,-17.65,-4.45,-1.55,5.15)
base=base.cut(usb_a_clear);lid=lid.cut(usb_a_clear)
parts={'base':base.clean(),'lid':lid.clean(),**controls}
for n in ['usb_c_collar','usb_c_end_cap','usb_a_end_cap']:parts[n]=cq.importers.importStep(str(E/(n+'.step'))).val()
print('Loading current board',flush=True)
cache=Path('/tmp/fuse-vault-case-'+hashlib.sha256((SRC/'source-board.step').read_bytes()).hexdigest()[:12]+'.brep')
if cache.exists():reference=cq.Shape.importBrep(str(cache))
else:
 reference=cq.importers.importStep(str(SRC/'source-board.step')).val();reference.exportBrep(str(cache))
electronics=reference.Solids()
print('Checking',len(electronics),'electronics solids',flush=True)
checks=[];fail=[]
def check(label,a,b):
 v=overlap(a,b);checks.append({'check':label,'overlap_mm3':v})
 if v>1e-4:fail.append(checks[-1])
for n,s in parts.items():
 assert s.isValid() and len(s.Solids())==1,(n,len(s.Solids()))
for (n,s),(m,t) in itertools.combinations(parts.items(),2):check(n+' / '+m,s,t)
for n,s in parts.items():
 print('Checking electronics against',n,flush=True)
 for i,e in enumerate(electronics):
  v=overlap(s,e)
  if v>1e-4:fail.append({'check':n+' / electronics','solid':i,'overlap_mm3':v,'electronics_bounds':bounds(e)})
 check(n+' / display reserve',s,display)
# Nominal switch actuator envelopes are used in addition to library geometry.
for n in ['SW4','SW5','SW6','SW7','SW8','SW9']:
 x,y=ns['coords'][n];act=cyl(x,y,.85,3.1,3.5)
 for label,s in parts.items():check(label+' / '+n+' actuator',s,act)
for ax in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for ang in [-6,-4,-2,0,2,4,6]:
  t=controls['dpad'].rotate((58.77,-10.38,2.55),(58.77+ax[0],-10.38+ax[1],2.55),ang)
  for name in ['lid','base','select','back']:check('Dpad tilt '+str(ax)+' '+str(ang)+' / '+name,t,parts[name])
for n in ['select','back']:
 for dz in [-.6,-.3,0]:check(n+' guide travel '+str(dz),controls[n].translate((0,0,dz)),parts['lid'])
# Conservative clear paths for board loading and card withdrawal.
check('vertical board loading envelope',parts['base'],box(-.127,73.208,-21.237,0,.001,16))
check('vertical SD loading envelope',parts['base'],sd_loading)
card_sweep=box(51.15,68.05,-.5,25,-2.4,.9)
for n in ['base','lid']:check('SD withdrawal / '+n,parts[n],card_sweep)
# Collar capture remains positive in both axial directions.
for dx in [-.5,.5]:
 v=sum(overlap(parts['usb_c_collar'].translate((dx,0,0)),parts[n]) for n in ['base','lid'])
 assert v>1e-4,('Lost collar capture',dx)
report={'revision':'F','checks':checks,'failures':fail,'controls_reused_from':'control-lid-test, translated -2.05 mm in Z','body_height_mm':12.1,'roof_control_thickness_mm':1.25,'display':'Previous provisional display envelope retained; not measured anew','limitations':['Actual switch feel/return and enclosure snap strength require hardware testing.','Current library 3D models are not a guarantee of manufacturer tolerances.','Flat-bottom printed PCB dummy is not suitable for testing the real underside supports.']}
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('FAILURES',json.dumps(fail,indent=2),flush=True)
if fail:raise RuntimeError('Review interference report before exporting')
# Export STEP in assembly coordinates, STLs oriented for printing.
for n,s in parts.items():
 cq.exporters.export(s,str(OUT/(n+'.step')))
 if n in ['lid','dpad','select','back']:p=s.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':p=s.rotate((0,0,0),(0,1,0),-90)
 elif n in ['usb_c_collar','usb_a_end_cap']:p=s.rotate((0,0,0),(0,1,0),90)
 else:p=s
 b=p.BoundingBox();p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(OUT/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(str(OUT/(n+'.stl')),force='mesh');mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 assert mesh.is_watertight and len(mesh.split())==1,n
 mesh.apply_translation(-mesh.bounds[0]);mesh.export(str(OUT/(n+'.stl')))
 report.setdefault('print_meshes',{})[n]={'watertight':True,'one_body':True,'size_mm':mesh.extents.tolist()}
assembly=cq.Assembly(name='fuse_vault_case_F')
colors={'base':(.25,.31,.38),'lid':(.55,.64,.73),'dpad':(.90,.62,.2),'select':(.9,.62,.2),'back':(.9,.62,.2)}
for n,s in parts.items():assembly.add(s,name=n,color=cq.Color(*colors.get(n,(.35,.43,.48))))
assembly.export(str(OUT/'case-assembly.step'))
assembly.add(reference,name='current_board',color=cq.Color(.2,.4,.3));assembly.export(str(OUT/'case-with-board.step'))
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in before.items()),'Baseline modified'
print('Exported validated full case',flush=True)
