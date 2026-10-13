from pathlib import Path
import json,math,zipfile
import cadquery as cq
import trimesh
R=Path(__file__).resolve().parent
D=R.parent/'pcb-print-model'
M=json.loads((D/'measurements.json').read_text())
C={c['ref']:c for c in M['components']}
coords={n:tuple(C[n]['origin_mm']) for n in ['SW4','SW5','SW6','SW7','SW8','SW9']}
cx,cy=58.77,-10.38
zboard=3.65; zswitch=5.55
roofbottom=8.6; rooftop=10.0
def box(x0,x1,y0,y1,z0,z1):return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def cyl(x,y,r,z0,z1):return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()
def crosspts(span,width):
 a=span/2;b=width/2
 return [(-b,-a),(b,-a),(b,-b),(a,-b),(a,b),(b,b),(b,a),(-b,a),(-b,b),(-a,b),(-a,-b),(-b,-b)]
def cross(span,width,z0,z1):return cq.Workplane('XY').polyline(crosspts(span,width)).close().extrude(z1-z0).translate((cx,cy,z0)).val()
# Side rails stand on the same flat surface as the dummy; 0.3 mm lateral gap.
ymin,ymax=M['bounds_mm'][2:4]
usb_start=C['J1']['bounds_print_mm'][0]
xmin,xmax=44.0,usb_start
lid=box(xmin,xmax,ymin-1.9,ymax+1.9,roofbottom,rooftop)
for y0,y1 in [(ymin-1.9,ymin-.3),(ymax+.3,ymax+1.9)]:lid=lid.fuse(box(xmin,xmax,y0,y1,0,roofbottom+.05))
# USB-A housing is the longitudinal datum, touching the right-hand stop face.
lid=lid.fuse(box(usb_start-.6,usb_start,-12.5,-8.2,zboard+.2,roofbottom+.05))
lid=lid.cut(cross(17.5,7.0,8.5,10.2))
buttons={}
for name,n in [('select','SW9'),('back','SW4')]:
 x,y=coords[n]
 sleeve=cyl(x,y,3.95,6.2,8.65).cut(cyl(x,y,3.45,6.1,8.65))
 lid=lid.fuse(sleeve).cut(cyl(x,y,2.5,8.5,10.2))
 # Common button: a 0.3 mm radial guide gap, retained by its collar.
 p=cyl(x,y,2.2,8.45,10.9).fuse(cyl(x,y,3.1,6.8,7.6))
 cone=cq.Solid.makeCone(3.1,2.2,.9,cq.Vector(x,y,7.6))
 p=p.fuse(cone,cyl(x,y,.85,5.8,6.85)).clean();buttons[name]=p
# Print upside-down: gradual skirt expansion avoids unsupported flange ledges.
cap=cross(16,5.5,7.9,11.0)
skirt=cq.Workplane('XY').workplane(offset=7.0).polyline(crosspts(18.2,7.6)).close().workplane(offset=.9).polyline(crosspts(16,5.5)).close().loft().translate((cx,cy,0)).val()
dpad=cap.fuse(skirt,cross(18.2,7.6,6.4,7.02))
for n in ['SW5','SW6','SW7','SW8']:
 x,y=coords[n];dpad=dpad.fuse(cyl(x,y,.85,5.8,6.5))
# Rounded central heel rests on the PCB surface, with no hole required.
dpad=dpad.fuse(cyl(cx,cy,.95,4.6,6.5),cq.Workplane('XY').sphere(.95).translate((cx,cy,4.6)).val()).clean()
# Relieve the hidden skirt locally around the two button guide sleeves.
for n in ['SW4','SW9']:
 x,y=coords[n];dpad=dpad.cut(cyl(x,y,4.45,6.3,11.1)).clean()
dpad=dpad.cut(cyl(cx+6,cy,.45,10.7,11.1)).clean() # USB-facing orientation dot
lid=lid.clean()
parts={'lid-section':lid,'dpad':dpad,**buttons}
def bb(s):
 b=s.BoundingBox();return [b.xmin,b.xmax,b.ymin,b.ymax,b.zmin,b.zmax]
checks=[]
for n,s in parts.items():
 assert s.isValid() and len(s.Solids())==1,n
 # Outer face / thumb surface flat against bed, all stems facing upwards.
 p=s.rotate((0,0,0),(1,0,0),180);b=p.BoundingBox();p=p.translate((-b.xmin,-b.ymin,-b.zmin))
 cq.exporters.export(p,str(R/(n+'.stl')),tolerance=.01,angularTolerance=.1)
 cq.exporters.export(p,str(R/(n+'.step')))
 mesh=trimesh.load(str(R/(n+'.stl')),force='mesh')
 mesh.update_faces(mesh.nondegenerate_faces());mesh.remove_unreferenced_vertices();mesh.merge_vertices()
 assert mesh.is_watertight and len(mesh.split())==1,n
 mesh.export(str(R/(n+'.stl')))
 checks.append({'part':n,'solid_valid':True,'watertight':True,'one_body':True,'print_bounds_mm':bb(p)})
for n,a in parts.items():
 for m,b in parts.items():
  if n<m:
   v=a.intersect(b).Volume();assert v<1e-5,(n,m,v)
# Full switch compression is deliberately not tested against the rigid dummy.
dummy=cq.importers.importStep(str(D/'pcb-fit-dummy.step')).val()
for n,s in parts.items():
 v=s.intersect(dummy).Volume();assert v<1e-5,('dummy collision',n,v)
# Test the lid and other controls throughout plausible travel on real switches.
sweep=[]
for ax in [(1,0,0),(0,1,0),(1,1,0),(1,-1,0)]:
 for deg in [-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6]:
  p=dpad.rotate((cx,cy,4.6),(cx+ax[0],cy+ax[1],4.6),deg)
  for n,o in [('lid',lid),*buttons.items()]:
   v=p.intersect(o).Volume()
   if v>1e-5:sweep.append({'axis':ax,'degrees':deg,'obstacle':n,'volume':v})
assert not sweep, sweep
for p in buttons.values():
 for dz in [0,-.25,-.5,-.6]:assert p.translate((0,0,dz)).intersect(lid).Volume()<1e-5
report={'checks':checks,'resting_interference_mm3':0,'tilt_interferences':sweep,'switch_centres_mm':coords,'pivot_mm':[cx,cy,4.6],'roof_z_mm':[roofbottom,rooftop],'plunger_gap_mm':.25,'guide_radial_gap_mm':.3,'dummy_source':'../pcb-print-model/pcb-fit-dummy.step','note':'Fit prototype; no claim of proven switch feel or force. Dummy actuators are rigid.'}
(R/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assembly=cq.Assembly(name='control_lid_fit_test')
assembly.add(dummy,name='printed_dummy',color=cq.Color(.23,.42,.37))
for n,s in parts.items():assembly.add(s,name=n,color=cq.Color(.85,.65,.2) if n!='lid-section' else cq.Color(.66,.72,.78))
assembly.export(str(R/'assembled-reference.step'))
# Offscreen preview, exploded vertically to show all captive pieces.
import vtk
ren=vtk.vtkRenderer();ren.SetBackground(.95,.96,.98)
items=[(dummy,(.23,.42,.37)),(lid,(.66,.72,.78)),(dpad,(.88,.61,.20)),*[(s,(.88,.61,.20)) for s in buttons.values()]]
for s,col in items:
 vs,ts=s.tessellate(.04);pts=vtk.vtkPoints();cells=vtk.vtkCellArray()
 for v in vs:pts.InsertNextPoint(*v.toTuple())
 for t in ts:
  cells.InsertNextCell(3)
  for i in t:cells.InsertCellPoint(i)
 p=vtk.vtkPolyData();p.SetPoints(pts);p.SetPolys(cells);mapper=vtk.vtkPolyDataMapper();mapper.SetInputData(p);a=vtk.vtkActor();a.SetMapper(mapper);a.GetProperty().SetColor(*col);ren.AddActor(a)
w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(1400,1000);w.AddRenderer(ren)
cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetPosition(107,-95,85);cam.SetFocalPoint(56,-10,5);cam.SetViewUp(0,0,1);cam.SetParallelScale(28);ren.ResetCameraClippingRange();w.Render()
f=vtk.vtkWindowToImageFilter();f.SetInput(w);f.Update();o=vtk.vtkPNGWriter();o.SetFileName(str(R/'preview.png'));o.SetInputConnection(f.GetOutputPort());o.Write();w.Finalize()
print(json.dumps(report,indent=2))
