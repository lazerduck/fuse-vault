"""Read-only baseline E -> separate dimensioned D-pad packaging concept.
Run using CadQuery 2.8, VTK, Pillow. Not a fabrication release.
"""
from pathlib import Path
import csv,json,math,hashlib
import cadquery as cq
import vtk
from PIL import Image,ImageDraw,ImageFont
R=Path(__file__).resolve().parent; E=R.parent/'prototype-e'
C=(61.1,-11.8); B=(49.149,-15.875); S=(49.149,-6.2)
orig={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in E.rglob('*') if p.is_file()}
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def cross(span,width,z0,z1):
 x,y=C
 return box(x-span/2,x+span/2,y-width/2,y+width/2,z0,z1).fuse(box(x-width/2,x+width/2,y-span/2,y+span/2,z0,z1)).clean()
def bb(s):
 b=s.BoundingBox();return [b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
parts={n:cq.importers.importStep(str(E/(n+'.step'))).val() for n in ('base','lid','usb_c_collar','usb_a_end_cap','usb_c_end_cap')}
reference=cq.importers.importStep(str(E/'reference/reference.step')).val()
rows=list(csv.DictReader((E/'reference/component_envelopes.csv').open()))
def rowshape(r):return box(*[float(r[k]) for k in ('xmin','xmax','ymin','ymax','zmin','zmax')])
# Associate original individual solids with source-derived component envelopes.
solids={}
for solid in reference.Solids():
 dims=bb(solid)
 best=min(rows,key=lambda r:sum(abs(a-float(r[k])) for a,k in zip(dims,('xmin','xmax','ymin','ymax','zmin','zmax'))))
 solids.setdefault(best['name'],[]).append(solid)
# Switch body and land reserves: KMR2 representative envelope, not vendor-detail CAD.
switches={}; lands={}
for n,x,y,vertical in [('up',C[0],C[1]+5.5,False),('down',C[0],C[1]-5.5,False),('left',C[0]-5.5,C[1],True),('right',C[0]+5.5,C[1],True),('select',*S,False)]:
 w,h=(2.8,4.2) if vertical else (4.2,2.8)
 switches[n]=box(x-w/2,x+w/2,y-h/2,y+h/2,1.6,3.0).fuse(cyl(x,y,.85,3.0,3.5))
 # 5 x 3.2 mm layout reserve, swapped to keep long side tangential.
 lw,lh=(3.2,5.0) if vertical else (5.0,3.2)
 lands[n]=box(x-lw/2,x+lw/2,y-lh/2,y+lh/2,1.6,2.6)
collisions=[]
for n,land in lands.items():
 for r in rows:
  if r['side']=='T' and r['name']!='SW3':
   b=land.BoundingBox()
   courtyard=box(b.xmin-.3,b.xmax+.3,b.ymin-.3,b.ymax+.3,1.6,3.6)
   v=courtyard.intersect(rowshape(r)).Volume()
   if v>1e-5:collisions.append({'new_switch':n,'existing_part':r['name'],'overlap_mm3':round(v,5)})
relocate=sorted({c['existing_part'] for c in collisions})
refs=[s for n,ss in solids.items() if n not in ['SW3']+relocate for s in ss]
# Local roof rebuild removes long Back flexure, its supports and joystick hole.
lid=parts['lid'].cut(box(45.45,69.1,-20.35,-2.1,5.5,9))
lid=lid.fuse(box(45.45,69.1,-20.35,-2.1,6.6,7.8))
lid=lid.cut(cross(17.2,6.7,6.2,10)).cut(cyl(*C,7.9,5.3,7.05))
# Printed pad: circular hidden flange, four plungers, integral rounded central heel.
flange=cyl(*C,7.3,5.4,6.2)
cap=cq.Workplane(obj=cross(16,5.5,6.15,8.4)).faces('>Z').edges().chamfer(.3).val()
pad=cap.fuse(flange)
pivot=cyl(*C,.8,2.4,5.5).fuse(cq.Workplane('XY').sphere(.8).val().translate((*C,2.4)))
pad=pad.fuse(pivot)
for x,y in [(C[0],C[1]+5.5),(C[0],C[1]-5.5),(C[0]-5.5,C[1]),(C[0]+5.5,C[1])]:pad=pad.fuse(cyl(x,y,.75,3.65,5.5))
# Captive individual plungers; guides and travel shelves are separate from switch actuators.
buttons={}
for n,(x,y),bottom in [('back',B,5.55),('select',S,3.65)]:
 sleeve=cyl(x,y,3.7,5.1,6.7).cut(cyl(x,y,3.25,5.5,7.2)).cut(cyl(x,y,1.3,5,7.2))
 lid=lid.fuse(sleeve).cut(cyl(x,y,2.5,6.85,10)).cut(cyl(x,y,3.25,5.5,6.95))
 buttons[n]=cyl(x,y,2.2,6.8,8.4).fuse(cyl(x,y,3.05,6.05,6.85),cyl(x,y,.85,bottom,6.1)).clean()
lid=lid.clean();pad=pad.clean()
static=[]
obstacles=[('lid',lid),('base',parts['base'])]+list(buttons.items())+[(n,s) for n,ss in solids.items() if n not in ['SW3']+relocate for s in ss]
for n,s in obstacles:
 v=pad.intersect(s).Volume()
 if v>1e-5:static.append({'part':n,'volume_mm3':v})
motion=[]
max_z=8.4
# Full cardinal actuation sweep; switch spring compression is intentionally allowed.
# Tilt about the spherical heel centre, keeping its bottom tangent to the PCB.
for axis in [(1,0,0),(0,1,0)]:
 for angle in [-4,-3,-2,-1,0,1,2,3,4]:
  tilted=pad.rotate((*C,2.4),(C[0]+axis[0],C[1]+axis[1],2.4),angle)
  max_z=max(max_z,tilted.BoundingBox().zmax)
  for n,s in obstacles:
   v=tilted.intersect(s).Volume()
   if v>1e-5:motion.append({'axis':axis,'angle':angle,'part':n,'volume_mm3':round(v,6)})
button_checks=[]
for n,s in buttons.items():
 for other,shape in [('lid',lid),('pad',pad),('other_button',buttons['select' if n=='back' else 'back'])]:
  v=s.intersect(shape).Volume()
  if v>1e-5:button_checks.append({'button':n,'part':other,'volume_mm3':round(v,6)})
# Model local board contact as a flat soldermask bearing area, not a through-hole.
newparts={'base':parts['base'],'lid_concept':lid,'dpad_concept':pad,'select_plunger':buttons['select'],'back_plunger':buttons['back'],'usb_c_collar':parts['usb_c_collar']}
for n,s in newparts.items():
 assert s.isValid() and len(s.Solids())==1,(n,'invalid/disconnected')
 cq.exporters.export(s,str(R/(n+'.step')))
assembly=cq.Assembly(name='Dpad_packaging_concept_not_fabrication_release')
col={'base':(.20,.27,.33),'lid_concept':(.61,.69,.76),'dpad_concept':(.10,.19,.25),'select_plunger':(.95,.57,.16),'back_plunger':(.28,.36,.42),'usb_c_collar':(.95,.57,.16)}
for n,s in newparts.items():assembly.add(s,name=n,color=cq.Color(*col[n]))
for i,s in enumerate(refs):assembly.add(s,name='existing_'+str(i),color=cq.Color(.22,.43,.32))
for n,s in switches.items():assembly.add(s,name='new_switch_'+n,color=cq.Color(.75,.70,.43))
assembly.export(str(R/'assembly-concept.step'))
# Summary includes all failures; a packaging study must not silently claim a fit.
report={'baseline':'Prototype E','units':'mm','case_body':[77.3,27.6,12.1],'proposed_body':[77.3,27.6,12.1], 'old_overall_bare_stick_height':14.9,'new_overall_neutral_height':12.7,'new_pad_protrusion':.6,'pad_span':16,'pad_arm_width':5.5,'hidden_flange_diameter':14.6,'flange_pocket_diameter':15.8,'opening_span':17.2,'opening_arm_width':6.7,'pad_centre':C,'old_joystick_centre':[60.325,-14.732],'centre_shift':[C[0]-60.325,C[1]+14.732],'select_centre':S,'back_centre':B,'button_diameter':4.4,'button_centres_distance':abs(S[1]-B[1]),'switch_pitch_across':11,'switch_land_reserve_span':[14.2,14.2],'switch_courtyard_reserve_span':[14.8,14.8],'courtyard_margin':.3,'pcb_top':1.6,'switch_top':3.5,'plunger_rest_bottom':3.65,'roof_top':7.8,'pad_top':8.4,'relocate_components':relocate,'new_lands_vs_existing':collisions,'pad_static_collisions':static,'pad_cardinal_sweep_collisions':motion,'button_static_collisions':button_checks,'max_height_during_sampled_tilt':max_z+4.3,'limitations':['Concept only; original PCB requires replacement of SW3 with four switch footprints and a new Select input footprint.','Simplified switch bodies and 5 x 3.2 mm land reserves; verify final selected manufacturer land pattern and pad numbering.','Routed PCB tracks, pullups, vias and soldermask bearing area are not redesigned.','Four cardinal sweeps only; diagonal behaviour, lift/retention, stops, tolerance stack and cap insertion not qualified.','Hard-stop tuning and switch preload require a small physical coupon; not print-ready.','Baseline electronics geometry includes provisional heights. Existing underside SD is unchanged; no centre through-hole is introduced.']}
(R/'measurements.json').write_text(json.dumps(report,indent=2)+'\n')
assert not static and not motion and not button_checks, 'Concept has unresolved collisions; inspect report'
print(json.dumps(report,indent=2),flush=True)
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in orig.items()),'baseline modified'
# Render CAD geometry, then compose labels and dimensions in a separate image.
def actor(shape,color):
 pts=vtk.vtkPoints();cells=vtk.vtkCellArray();vs,ts=shape.tessellate(.07,.15)
 for v in vs:pts.InsertNextPoint(*v.toTuple())
 for t in ts:
  cells.InsertNextCell(3)
  for i in t:cells.InsertCellPoint(i)
 mesh=vtk.vtkPolyData();mesh.SetPoints(pts);mesh.SetPolys(cells)
 normals=vtk.vtkPolyDataNormals();normals.SetInputData(mesh);normals.SetFeatureAngle(35)
 mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(normals.GetOutputPort())
 a=vtk.vtkActor();a.SetMapper(mapper);a.GetProperty().SetColor(*color);a.GetProperty().SetAmbient(.35);return a
rendernew=[(s,col[n]) for n,s in newparts.items()]+[(s,(.22,.43,.32)) for s in refs]+[(s,(.74,.70,.44)) for s in switches.values()]
renderold=[(parts[n],col.get(n,(.61,.69,.76))) for n in ('base','lid','usb_c_collar')]+[(reference,(.22,.43,.32))]
def render(name,items,position,focal,scale,size=(1300,650),up=(0,0,1)):
 w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(*size);w.SetMultiSamples(8)
 ren=vtk.vtkRenderer();ren.SetBackground(.949,.961,.976);w.AddRenderer(ren)
 for s,c in items:ren.AddActor(actor(s,c))
 cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetPosition(*position);cam.SetFocalPoint(*focal);cam.SetViewUp(*up);cam.SetParallelScale(scale);ren.ResetCameraClippingRange();w.Render()
 cap=vtk.vtkWindowToImageFilter();cap.SetInput(w);cap.Update();out=vtk.vtkPNGWriter();out.SetFileName(str(R/name));out.SetInputConnection(cap.GetOutputPort());out.Write();w.Finalize()
render('proposed.png',rendernew,(-10,-105,100),(37,-10,2),27)
render('current.png',renderold,(-10,-105,100),(37,-10,2),27)
render('top.png',rendernew,(37,-10,100),(37,-10,0),20,(1500,650),(0,1,0))
# Local exploded view exposes flange and PCB-facing plungers.
local=box(44.5,70,-22,0,-1,12)
exploded=[(s.intersect(local),c) for s,c in rendernew if s not in [lid,pad,*buttons.values(),parts['base']] and s.intersect(local).Volume()>1e-5]
exploded += [(pad.translate((0,0,12)),col['dpad_concept'])]+[(s.translate((0,0,7)),col[n+'_plunger']) for n,s in buttons.items()]
render('mechanism.png',exploded,(90,-60,43),(58,-11,9),20,(900,800))
# Dimensional plan using the actual CAD top as its base.
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';bold='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def f(n,b=False):return ImageFont.truetype(bold if b else font,n)
page=Image.new('RGB',(1800,1630),'#f2f5f9');d=ImageDraw.Draw(page)
d.text((55,35),'16 mm D-pad / current case',font=f(42,True),fill='#162936')
d.text((55,93),'CAD packaging study  •  Prototype E baseline  •  millimetres',font=f(23),fill='#536777')
for x,name,title in [(30,'current.png','CURRENT  /  9 mm stick'),(910,'proposed.png','PROPOSED  /  separate Select')]:
 im=Image.open(R/name).resize((860,430));page.paste(im,(x,180));d.text((x+25,145),title,font=f(25,True),fill='#162936')
d.text((65,610),'77.3 × 27.6 mm body  |  14.9 mm to stick tip',font=f(22),fill='#536777')
d.text((945,610),'Same body  |  12.7 mm to pad top at rest',font=f(22),fill='#536777')
d.line((55,670,1745,670),fill='#c9d4dc',width=2)
d.text((55,700),'TOP VIEW / proposed placement',font=f(26,True),fill='#162936')
im=Image.open(R/'top.png').resize((1120,485));page.paste(im,(15,775))
# Top image mapping is 650/(2*20)=16.25 px/mm before resize.
k=485/40
def xy(x,y):return (15+560+(x-37)*k,775+242.5-(y+10)*k)
def leader(label,x,y,tx,ty):
 px,py=xy(x,y);d.line((px,py,tx-12,ty+12),fill='#566d7b',width=2);d.ellipse((px-4,py-4,px+4,py+4),fill='#566d7b');d.text((tx,ty),label,font=f(22),fill='#162936')
leader('Select / Ø4.4',*S,1240,860)
leader('16 mm cross',C[0],C[1],1240,950)
leader('Back / Ø4.4',*B,1240,1040)
d.text((1240,1150),'Pad moved 2.93 mm',font=f(21,True),fill='#162936');d.text((1240,1180),'away from lower edge',font=f(21),fill='#536777')
d.line((55,1305,1745,1305),fill='#c9d4dc',width=2)
texts=[('0 mm','added body length / width'),('2.2 mm','less height at rest'),('14.2 × 14.2 mm','direction-switch land envelope')]
for x,(big,small) in zip((65,590,1110),texts):d.text((x,1345),big,font=f(35,True),fill='#162936');d.text((x,1395),small,font=f(21),fill='#536777')
d.text((55,1485),'Requires PCB changes and a new lid. This is a packaging mock-up, not a drop-in or print-ready conversion.',font=f(22,True),fill='#8a5021')
d.text((55,1530),'Flange, tilt clearances, switch lands and component clashes checked separately in measurements.json.',font=f(21),fill='#536777')
page.save(R/'mockup.png')

# Section through left/right switches and the rounded heel.
sectionbox=box(51,71,C[1],C[1]+.1,-1,12)
section=[(s.intersect(sectionbox),c) for s,c in rendernew if s.intersect(sectionbox).Volume()>1e-6]
render('section.png',section,(61,-110,4.5),(61,-11,4.5),5.8,(1300,550))
sheet=Image.new('RGB',(1800,1450),'#f2f5f9');d=ImageDraw.Draw(sheet)
d.text((55,35),'How the printed mechanism would work',font=f(38,True),fill='#162936')
d.text((55,95),'Actual concept CAD / simplified switch envelopes / no centre switch',font=f(23),fill='#536777')
im=Image.open(R/'mechanism.png').resize((810,720));sheet.paste(im,(25,130))
notes=[('One rigid printed cross','The four arms each have a small plunger.'),('Hidden retaining flange','A 14.6 mm disc stays beneath the lid.'),('Rounded central heel','Rocks on a clear area of the PCB surface.'),('Four off-the-shelf switches','11 mm between opposite switch centres.'),('Select stays independent','Separate plunger above the existing Back.') ]
for i,(title,detail) in enumerate(notes):
 y=180+i*112;d.text((865,y),title,font=f(26,True),fill='#162936');d.text((865,y+38),detail,font=f(23),fill='#536777')
d.line((55,855,1745,855),fill='#c9d4dc',width=2)
d.text((55,885),'SECTION / through the left and right switches',font=f(26,True),fill='#162936')
im=Image.open(R/'section.png').resize((1040,440));sheet.paste(im,(25,930))
for i,line in enumerate(['0.6 mm above lid at rest','0.15 mm nominal plunger gap','1.9 mm switch height','No through-hole under pivot','Underside SD area preserved']):d.text((1110,985+i*49),line,font=f(23),fill='#162936')
d.text((55,1378),'Still to prototype: travel stops, return/preload, diagonal presses, wear and real printer tolerances.',font=f(23,True),fill='#8a5021')
sheet.save(R/'execution.png')
