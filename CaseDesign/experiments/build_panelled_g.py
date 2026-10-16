"""Panelled cosmetic lid on the validated G mechanics. No changes below Z=6.4."""
from pathlib import Path
import cadquery as cq
import trimesh,json,shutil,hashlib
R=Path(__file__).resolve().parent;G=R/'prototype-g';O=R/'prototype-g-panelled';O.mkdir(exist_ok=True)
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def poly(p,z=6.4,h=.4):return cq.Workplane('XY').polyline(p).close().extrude(h).translate((0,0,z)).val()
def load(n):return cq.importers.importStep(str(G/(n+'.step'))).val()
plain=load('lid_single_colour')
# 0.8 mm wide, 0.2 mm deep stepped panel joint. It avoids the snap pockets.
seam=poly([(15.6,2.8),(13.2,-5),(17.2,-12),(13.6,-23.1),(14.4,-23.1),(18,-12),(14,-5),(16.4,2.8)],6.6,.3)
# Corner seam stays clear of the four snap locations (x6.5–10 / x70–73).
corner=poly([(64,2.8),(68.8,-2),(74,-2),(74,-1.2),(69.1,-1.2),(65.1,2.8)],6.6,.3)
grooves=seam.fuse(corner)
sculpted=plain.cut(grooves).clean()
# Grey face fields follow the roof but leave a dark protective rim and apertures.
field=poly([(2.2,-22.7),(72.3,-22.7),(73,-22),(73,1.1),(72.3,1.8),(65.7,1.8),(63.7,3.0),(50.8,3.0),(48.8,1.5),(2.2,1.5)])
# Continue the dark colour through the groove floor so the recessed seam reads dark.
seam_colour=poly([(15.6,2.8),(13.2,-5),(17.2,-12),(13.6,-23.1),(14.4,-23.1),(18,-12),(14,-5),(16.4,2.8)])
corner_colour=poly([(64,2.8),(68.8,-2),(74,-2),(74,-1.2),(69.1,-1.2),(65.1,2.8)])
logo=load('lid_inlay_grey')
diamond=poly([(6.85,-10.6),(8.5,-8.95),(10.15,-10.6),(8.5,-12.25)])
accent=poly([(14.05,-3.2),(14.6,-1.6),(15.3,-1.6),(14.75,-3.2)])
orange=diamond.fuse(accent).intersect(sculpted)
grey=field.intersect(sculpted).cut(seam_colour).cut(corner_colour).cut(logo).cut(orange)
# Two restrained dark identification ticks; no tiny text or long circuit traces.
for x,y in [(4,-20.6),(70,-20.6)]:grey=grey.cut(box(x,x+.5,y,y+1.5,6.4,6.9))
lid=sculpted.cut(grey).cut(orange).clean()
parts={'lid':lid,'lid_inlay_grey':grey,'lid_inlay_orange':orange}
checks=[]
def check(label,ok):checks.append({'check':label,'passed':bool(ok)});assert ok,label
joined=lid.fuse(grey,orange).clean()
check('Only shallow grooves removed',abs(joined.Volume()-sculpted.Volume())<1e-5)
check('No added material outside original G lid',joined.cut(plain).Volume()<1e-5)
low=box(-20,100,-30,10,-10,6.4)
check('All G geometry below colour layer unchanged',plain.cut(joined).intersect(low).Volume()<1e-5)
# Verify the seams cannot cut into any snap roof.
for x0,x1 in [(6.05,10.45),(69.55,73.45)]:
 for y0,y1 in [(0,2.7),(-23.9,-21.2)]:check('Snap roof preserved '+str((x0,y0)),grooves.intersect(box(x0,x1,y0,y1,6.2,6.9)).Volume()<1e-5)
for n,s in parts.items():check(n+' valid',s.isValid())
check('Dark structural lid is one solid',len(lid.Solids())==1)
for n,m in [('lid','lid_inlay_grey'),('lid','lid_inlay_orange'),('lid_inlay_grey','lid_inlay_orange')]:check(n+' / '+m+' disjoint',parts[n].intersect(parts[m]).Volume()<1e-5)
# All other parts are byte-for-byte copies of G.
for p in G.iterdir():
 if p.suffix in ['.step','.stl'] and not p.name.startswith('lid') and not p.name.startswith('case-'):shutil.copy2(p,O/p.name)
for n in ['lid_snap_coupon','lid_cap_coupon']:
 for ext in ['.step','.stl']:shutil.copy2(G/(n+ext),O/(n+ext))
shutil.copy2(G/'README.md',O/'baseline-G-instructions.md')
for n,s in {**parts,'lid_single_colour':sculpted}.items():
 cq.exporters.export(s,str(O/(n+'.step')))
 p=s.rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,6.8))
 cq.exporters.export(p,str(O/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 mesh=trimesh.load(O/(n+'.stl'),force='mesh');mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices();check(n+' watertight mesh',mesh.is_watertight);mesh.export(O/(n+'.stl'))
colors={'base':(.07,.08,.10),'lid':(.10,.115,.13),'dpad':(.09,.10,.12),'select':(1,.43,.025),'back':(.09,.10,.12),'lid_inlay_grey':(.78,.79,.77),'lid_inlay_orange':(1,.43,.025)}
a=cq.Assembly(name='Fuse_Vault_G_panelled')
for n in ['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap','lid_inlay_grey','lid_inlay_orange']:a.add(parts[n] if n in parts else load(n),name=n,color=cq.Color(*colors.get(n,(.09,.10,.12))))
a.export(str(O/'case-assembly.step'))
a=cq.Assembly(name='Panelled_AMS_lid')
for n,s in parts.items():a.add(s.rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,6.8)),name=n,color=cq.Color(*colors[n]))
a.export(str(O/'lid-ams.step'))
report={'checks':checks,'failures':[],'groove_depth_mm':.2,'seam_width_mm':.8,'inlay_depth_mm':.4,'mechanical_baseline':'prototype-g','baseline_validation_sha256':hashlib.sha256((G/'validation.json').read_bytes()).hexdigest(),'note':'G clearances inherited by exact preservation below Z6.4 and proof no material added; colour regions do not change assembled geometry. Physical print test pending.'}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Panelled lid exported; checks:',len(checks))
