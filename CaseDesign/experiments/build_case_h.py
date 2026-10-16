"""H: compact control guides and one-piece recessed panel geometry."""
from pathlib import Path
import cadquery as cq
import trimesh,json,hashlib,itertools
R=Path(__file__).resolve().parent;G=R/'prototype-g';O=R/'prototype-h';O.mkdir(exist_ok=True)
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def poly(p,z,h):return cq.Workplane('XY').polyline(p).close().extrude(h).translate((0,0,z)).val()
def load(n):return cq.importers.importStep(str(G/(n+'.step'))).val()
def crosspts(span,w):
 a=span/2;b=w/2
 return [(-b,-a),(b,-a),(b,-b),(a,-b),(a,b),(b,b),(b,a),(-b,a),(-b,b),(-a,b),(-a,-b),(-b,-b)]
def cross(span,w,z0,z1):return poly(crosspts(span,w),z0,z1-z0).translate((58.77,-10.38,0))
def flip(s):return s.rotate((0,-10.6,1.25),(1,-10.6,1.25),180)
base=load('base');lid=load('lid_single_colour');collar=load('usb_c_collar')
# Replace the crowded control zone with a continuous roof and smaller guides.
support=lid.intersect(box(68.2,69.8,-.75,1.35,1.8,6.8))
lid=lid.cut(box(44.25,69.15,-20.65,-.25,3,9)).fuse(box(44.25,69.15,-20.65,-.25,5.55,6.8),support)
coords={'select':(48.26,-4.318),'back':(48.895,-16.637)}
controls={}
for n,(x,y) in coords.items():
 sleeve=cyl(x,y,3.35,4.0,5.65).cut(cyl(x,y,2.85,3.95,5.65))
 lid=lid.fuse(sleeve).cut(cyl(x,y,2.15,5.4,7))
 p=cyl(x,y,1.85,5.45,7.65).fuse(cyl(x,y,2.55,4.0,4.8),cq.Solid.makeCone(2.55,1.85,.7,cq.Vector(x,y,4.8)),cyl(x,y,.85,3.75,4.05))
 controls[n]=p.clean()
lid=lid.cut(cross(16.0,6.4,5.4,7.0))
relief=cq.Workplane('XY').workplane(offset=5.35).polyline(crosspts(17,7.4)).close().workplane(offset=.65).polyline(crosspts(16,6.4)).close().loft().translate((58.77,-10.38,0)).val()
lid=lid.cut(relief).cut(box(16.85146,45.25146,-18.74208,-4.74208,1.85,5.45)).clean()
skirt=cq.Workplane('XY').workplane(offset=4.55).polyline(crosspts(16.4,6.8)).close().workplane(offset=.8).polyline(crosspts(14.8,5.2)).close().loft().translate((58.77,-10.38,0)).val()
dpad=cross(14.8,5.2,5.35,7.75).fuse(cross(16.4,6.8,4.0,4.56),skirt)
for x,y in [(58.801,-4.699),(53.086,-10.668),(59.055,-15.875),(64.135,-10.287)]:dpad=dpad.fuse(cyl(x,y,.85,3.75,4.1))
dpad=dpad.fuse(cyl(58.77,-10.38,.95,2.55,4.1),cq.Workplane('XY').sphere(.95).translate((58.77,-10.38,2.55)).val()).clean()
# Local hidden skirt clearance: retain the solid web between face openings.
for x,y in coords.values():dpad=dpad.cut(cyl(x,y,3.7,3.9,5.4)).clean()
controls['dpad']=dpad
# One solid lid. At 0.2 mm layers, first two layers are light grey;
# change to charcoal at Z=0.4 mm to colour the recessed floors and body.
seam_pts=[(15.6,2.8),(13.2,-5),(17.2,-12),(13.6,-23.1),(14.4,-23.1),(18,-12),(14,-5),(16.4,2.8)]
corner_pts=[(64,2.8),(68.8,-2),(74,-2),(74,-1.2),(69.1,-1.2),(65.1,2.8)]
logo=load('lid_inlay_grey')
diamond=poly([(6.85,-10.6),(8.5,-8.95),(10.15,-10.6),(8.5,-12.25)],6.4,.5)
grooves=poly(seam_pts,6.4,.5).fuse(poly(corner_pts,6.4,.5),logo,diamond)
plain_lid=lid
lid=lid.cut(grooves).clean()
parts={'base':base,'lid':lid,**controls,'usb_c_collar':collar}
# Matching small valleys on both sides of the flexible caps, no inserts.
for end,x in [('c',-8.8),('a',83.0)]:
 cap=load('usb_'+end+'_end_cap')
 path=[(x-.4,-21),(x+1.6,-13),(x-.4,-1),(x+.4,-1),(x+2.4,-13),(x+.4,-21)]
 groove=poly(path,6.4,.5)
 parts['usb_'+end+'_end_cap']=cap.cut(groove).cut(flip(groove)).clean()
print('Shapes and controls',flush=True)
checks=[];fail=[];cache={}
def bb(s):
 k=id(s)
 if k not in cache:
  b=s.BoundingBox();cache[k]=(s,[b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax])
 return cache[k][1]
def overlap(a,b):
 x,y=bb(a),bb(b)
 if any(x[i+1]<=y[i]+1e-6 or y[i+1]<=x[i]+1e-6 for i in [0,2,4]):return 0.
 return a.intersect(b).Volume()
def ck(label,a,b):
 v=overlap(a,b);checks.append({'check':label,'overlap_mm3':v})
 if v>1e-4:fail.append(checks[-1])
for n,s in parts.items():
 if not s.isValid() or (not n.startswith('lid_inlay') and len(s.Solids())!=1):fail.append({'invalid_or_disconnected':n})
for (n,s),(m,t) in itertools.combinations(parts.items(),2):ck(n+' / '+m,s,t)
for ax in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for ang in [-6,-4,-2,0,2,4,6]:
  t=dpad.rotate((58.77,-10.38,2.55),(58.77+ax[0],-10.38+ax[1],2.55),ang)
  for n in ['lid','base','select','back']:ck('tilt '+str(ax)+' '+str(ang)+' / '+n,t,parts[n])
for n in ['select','back']:
 for dz in [-.6,-.3,0]:ck(n+' travel '+str(dz),controls[n].translate((0,0,dz)),lid)
# Minimum face web between the actual aperture and each round button hole.
web={n:cross(16,6.4,6.4,6.8).distance(cyl(x,y,2.15,6.4,6.8)) for n,(x,y) in coords.items()}
if min(web.values())<1.2:fail.append({'face_web_below_1_2mm':web})
for x0,x1 in [(6.05,10.45),(69.55,73.45)]:
 for y0,y1 in [(0,2.7),(-23.9,-21.2)]:ck('Valleys avoid snap roof '+str((x0,y0)),grooves,box(x0,x1,y0,y1,6.2,6.9))
print('Electronics',flush=True)
refpath=R.parent/'pcb-print-model/source-board.step';cachefile=Path('/tmp/fuse-vault-case-'+hashlib.sha256(refpath.read_bytes()).hexdigest()[:12]+'.brep')
ref=cq.Shape.importBrep(str(cachefile)) if cachefile.exists() else cq.importers.importStep(str(refpath)).val();electronics=ref.Solids()
for n,s in parts.items():
 for i,e in enumerate(electronics):ck(n+' / electronics '+str(i),s,e)
 ck(n+' / raised display',s,box(16.95,45.15,-18.65,-4.84,3.6,5.4))
# Unchanged G seating geometry and simplified cap geometry during withdrawal.
for end in ['c','a']:
 cap=parts['usb_'+end+'_end_cap']
 if cap.cut(flip(cap)).Volume()>1e-4:fail.append({'cap_symmetry':end})
 for d in [0,.25,.5,.75,1,2,4,8,15]:
  shift=(-d if end=='c' else d,0,0)
  for n in ['usb_'+end+'_end_cap']:
   moved=parts[n].translate(shift)
   for i,e in enumerate(electronics):ck(n+' removal '+str(d)+' / electronics '+str(i),moved,e)
report={'revision':'H','checks':checks,'failures':fail,'button_to_dpad_face_web_mm':web,'body_height_mm':11.1,'valley_depth_mm':.4,'colour_change_print_z_mm':.4,'limitations':['No physical print or slicer-specific verification yet. Lid is one solid; shallow recessed floors bridge on the first charcoal layer.','G caps seating and shell snaps retained; new control feel and cap groove durability require testing.']}
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('FAILURES',fail,flush=True)
if fail:raise RuntimeError('Inspect validation')
# Preserve the shims and mechanical fit samples; replacement controls are required.
import shutil
for n in ['screen_shim_1.6mm','screen_shim_1.8mm','screen_shim_2.0mm','base_snap_coupon','lid_snap_coupon','base_cap_coupon','lid_cap_coupon']:
 for ext in ['.step','.stl']:shutil.copy2(G/(n+ext),O/(n+ext))
for n,s in parts.items():
 cq.exporters.export(s,str(O/(n+'.step')))
 if n.startswith('lid') or n in ['dpad','select','back']:p=s.rotate((0,0,0),(1,0,0),180)
 elif n=='usb_c_end_cap':p=s.rotate((0,0,0),(0,1,0),-90)
 elif n in ['usb_a_end_cap','usb_c_collar']:p=s.rotate((0,0,0),(0,1,0),90)
 else:p=s
 b=p.BoundingBox()
 if n=='lid':p=p.translate((2.5,3.9,6.8))
 else:p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(O/(n+'.stl')),tolerance=.015,angularTolerance=.1)
 m=trimesh.load(O/(n+'.stl'),force='mesh');m.update_faces(m.nondegenerate_faces());m.remove_unreferenced_vertices();m.merge_vertices();assert m.is_watertight,n;m.export(O/(n+'.stl'))
 report.setdefault('exports',{})[n]={'watertight':True,'solids':len(s.Solids())}
colors={'base':(.07,.08,.10),'lid':(.10,.115,.13),'dpad':(.09,.10,.12),'select':(1,.43,.025),'back':(.09,.10,.12),'lid_inlay_grey':(.78,.79,.77),'lid_inlay_orange':(1,.43,.025)}
a=cq.Assembly(name='Fuse_Vault_H')
for n,s in parts.items():a.add(s,name=n,color=cq.Color(*colors.get(n,(.78,.79,.77) if n.endswith('end_cap') else (.09,.10,.12))))
a.export(str(O/'case-assembly.step'))
(O/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('H exported',flush=True)
