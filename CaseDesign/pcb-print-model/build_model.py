"""Printable fit dummy. Coordinates from current EasyEDA export; dimensions in mm.
Run with cadquery 2.8, trimesh and vtk installed. No edits to the PCB project.
"""
from pathlib import Path
import json, math, re
import cadquery as cq
import trimesh
R=Path(__file__).resolve().parent
T=1.6
raw=json.loads((R/'source-placements.json').read_text())['result']
comps=[c for c in raw['components'] if c['layer']==1 and not c['footprint']['name'].startswith('Test-Point')]
src=cq.importers.importStep(str(R/'source-board.step')).val()
solids=src.Solids()
board=max(solids,key=lambda s:s.Volume()); b=board.BoundingBox()
source_top=b.zmax
# Ignore micron-thick model lettering below the SD housing.
bottom_depth=round(-min(s.BoundingBox().zmin for s in solids if s.Volume()>.01),2)
def box(x0,x1,y0,y1,z0,z1):
 return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def bounds(s):
 q=s.BoundingBox();return [q.xmin,q.xmax,q.ymin,q.ymax,q.zmin,q.zmax]
# Solid board/underside slab. Small drill holes are intentionally filled.
base=box(b.xmin,b.xmax,b.ymin,b.ymax,0,bottom_depth+T)
shapes=[base];items=[(base,(.22,.42,.38))];entries=[]
# USB connector outer envelopes: remove hollow interior and extend below to bed.
for name,ids in [('J1',[i for i,s in enumerate(solids) if s.BoundingBox().xmax>80]),('J2',[i for i,s in enumerate(solids) if s.BoundingBox().xmin<-5])]:
 ss=[solids[i] for i in ids]; q=cq.Compound.makeCompound(ss).BoundingBox()
 top=q.zmax+(T-source_top)+bottom_depth
 p=box(q.xmin,q.xmax,q.ymin,q.ymax,0,top)
 shapes.append(p);items.append((p,(.64,.68,.72)))
 entries.append({'ref':name,'bounds_print_mm':bounds(p),'representation':'solid exported connector envelope, extended down to print bed'})
# Match remaining top STEP solids to nearby component origins. These are used
# for envelope dimensions only; no miniature legs, cavities or floating lettering.
groups={c['designator']:[] for c in comps if c['designator'] not in ('J1','J2')}
for s in solids:
 q=s.BoundingBox()
 if q.zmax<=source_top+.01 or q.xmin<-5 or q.xmax>80 or s==board:continue
 x=(q.xmin+q.xmax)/2;y=(q.ymin+q.ymax)/2
 candidates=[c for c in comps if c['designator'] in groups]
 c=min(candidates,key=lambda c:(x-c['x']*.0254)**2+(y-c['y']*.0254)**2)
 groups[c['designator']].append(s)
for c in comps:
 n=c['designator']
 if n in ('J1','J2'):continue
 x,y=c['x']*.0254,c['y']*.0254
 if n.startswith('SW'):
  # Actual switch body and fixed actuator; leads omitted. Actuator diameter is
  # a simplified contact target, not a manufacturer surface-detail replica.
  p=box(-2.1,2.1,-1.4,1.4,bottom_depth+T-.02,bottom_depth+T+1.5)
  a=cq.Workplane('XY').circle(.85).extrude(.42).translate((0,0,bottom_depth+T+1.48)).val()
  p=p.fuse(a).rotate((0,0,0),(0,0,1),c['rotation']).translate((x,y,0))
  note='4.2 x 2.8 mm simplified housing; fixed 1.7 mm diameter actuator, 1.9 mm total height'
 else:
  assert groups[n],f'Missing model for {n}'
  q=cq.Compound.makeCompound(groups[n]).BoundingBox()
  height=q.zmax-source_top
  if n=='L1':height=2.0 # Datasheet maximum, library model is only 1.91 mm.
  p=box(q.xmin,q.xmax,q.ymin,q.ymax,bottom_depth+T-.02,bottom_depth+T+height)
  note='solid exported component envelope; L1 corrected to 2.0 mm' if n=='L1' else 'solid exported component envelope'
 shapes.append(p);items.append((p,(.91,.62,.23) if n.startswith('SW') else (.30,.33,.38)))
 entries.append({'ref':n,'origin_mm':[x,y],'bounds_print_mm':bounds(p),'representation':note})
print('Fusing',len(shapes),'pieces',flush=True)
model=shapes[0].fuse(*shapes[1:]).clean()
assert model.isValid() and len(model.Solids())==1,'Must be one valid connected solid'
cq.exporters.export(model,str(R/'pcb-fit-dummy.stl'),tolerance=.015,angularTolerance=.1)
cq.exporters.export(model,str(R/'pcb-fit-dummy.step'))
mesh=trimesh.load(str(R/'pcb-fit-dummy.stl'),force='mesh')
assert mesh.is_watertight and mesh.is_winding_consistent and len(mesh.split())==1
report={'pcb_nominal_thickness_mm':T,'underside_depth_mm':bottom_depth,'pcb_top_above_print_bed_mm':bottom_depth+T,'switch_top_above_print_bed_mm':bottom_depth+T+1.9,'bounds_mm':bounds(model),'valid_solid':model.isValid(),'solid_count':len(model.Solids()),'mesh_watertight':bool(mesh.is_watertight),'mesh_connected_bodies':len(mesh.split()),'components':entries,'limitations':['Rigid fit dummy; switches do not move.','Underside is a full slab, not bottom-component detail; this can obstruct existing case supports.','USB bodies are filled and extended to the bed; underside connector clearances cannot be tested.','Display and FPC are not in the PCB export and are not included.','Small packages use solid envelopes, switch actuator shape is simplified; heights depend on library models except nominal switch and L1 height corrections.','Small PCB holes and slots are filled.']}
(R/'measurements.json').write_text(json.dumps(report,indent=2)+'\n')
# Render the model geometry; colours only identify regions in preview.
import vtk
ren=vtk.vtkRenderer();ren.SetBackground(.95,.96,.98)
for s,col in items:
 vs,ts=s.tessellate(.03);pts=vtk.vtkPoints();cells=vtk.vtkCellArray()
 for v in vs:pts.InsertNextPoint(*v.toTuple())
 for t in ts:
  cells.InsertNextCell(3)
  for i in t:cells.InsertCellPoint(i)
 poly=vtk.vtkPolyData();poly.SetPoints(pts);poly.SetPolys(cells)
 mapper=vtk.vtkPolyDataMapper();mapper.SetInputData(poly)
 a=vtk.vtkActor();a.SetMapper(mapper);a.GetProperty().SetColor(*col);ren.AddActor(a)
w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(1600,650);w.AddRenderer(ren)
cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetPosition(8,-95,110);cam.SetFocalPoint(38,-10,2);cam.SetViewUp(0,0,1);cam.SetParallelScale(24);ren.ResetCameraClippingRange();w.Render()
f=vtk.vtkWindowToImageFilter();f.SetInput(w);f.Update();out=vtk.vtkPNGWriter();out.SetFileName(str(R/'preview.png'));out.SetInputConnection(f.GetOutputPort());out.Write();w.Finalize()
print(json.dumps({k:v for k,v in report.items() if k not in ('components','limitations')},indent=2))
