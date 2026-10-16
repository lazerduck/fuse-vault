"""Render actual G CAD; previews are not generated concept artwork."""
from pathlib import Path
import cadquery as cq
import vtk
R=Path(__file__).resolve().parent/'prototype-g-panelled'
def actor(s,color):
 vs,ts=s.tessellate(.05);pts=vtk.vtkPoints();cells=vtk.vtkCellArray()
 for v in vs:pts.InsertNextPoint(*v.toTuple())
 for t in ts:
  cells.InsertNextCell(3)
  for i in t:cells.InsertCellPoint(i)
 p=vtk.vtkPolyData();p.SetPoints(pts);p.SetPolys(cells);m=vtk.vtkPolyDataMapper();m.SetInputData(p);a=vtk.vtkActor();a.SetMapper(m);a.GetProperty().SetColor(*color);return a
names=['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap','lid_inlay_grey','lid_inlay_orange']
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in names}
cols={'base':(.07,.08,.10),'lid':(.12,.135,.16),'dpad':(.09,.10,.12),'select':(1,.43,.025),'back':(.09,.10,.12),'lid_inlay_grey':(.78,.79,.77),'lid_inlay_orange':(1,.43,.025)}
for mode in ['assembled','uncapped','exploded','top']:
 ren=vtk.vtkRenderer();ren.SetBackground(.93,.945,.96);w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(1700,950);w.AddRenderer(ren)
 for n,s in parts.items():
  if mode=='uncapped' and n.endswith('end_cap'):continue
  if mode=='exploded':
   if n.startswith('lid'):s=s.translate((0,0,12))
   elif n in ['dpad','select','back']:s=s.translate((0,0,6))
   elif n.endswith('end_cap'):s=s.translate((-10 if n=='usb_c_end_cap' else 10,0,0))
  ren.AddActor(actor(s,cols.get(n,(.09,.105,.125))))
 screen=cq.Workplane('XY').box(27.9,13.5,1.5,centered=False).translate((17.10146,-18.49208,3.6)).val()
 ren.AddActor(actor(screen,(.025,.10,.13)))
 cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetPosition(100,-115,105) if mode!='top' else cam.SetPosition(38,-10,160)
 cam.SetFocalPoint(38,-10,5 if mode=='exploded' else 0);cam.SetViewUp(0,0,1) if mode!='top' else cam.SetViewUp(0,1,0);cam.SetParallelScale(40 if mode=='exploded' else 34);ren.ResetCameraClippingRange();w.Render()
 f=vtk.vtkWindowToImageFilter();f.SetInput(w);f.Update();p=vtk.vtkPNGWriter();p.SetFileName(str(R/(mode+'.png')));p.SetInputConnection(f.GetOutputPort());p.Write();w.Finalize()
