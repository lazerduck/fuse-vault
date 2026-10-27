from pathlib import Path
import cadquery as cq
import vtk
import sys
R=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent/'prototype-k'
def actor(s,color):
 vs,ts=s.tessellate(.06);pts=vtk.vtkPoints();cells=vtk.vtkCellArray()
 for v in vs:pts.InsertNextPoint(*v.toTuple())
 for t in ts:
  cells.InsertNextCell(3)
  for i in t:cells.InsertCellPoint(i)
 p=vtk.vtkPolyData();p.SetPoints(pts);p.SetPolys(cells);m=vtk.vtkPolyDataMapper();m.SetInputData(p);a=vtk.vtkActor();a.SetMapper(m);a.GetProperty().SetColor(*color);return a
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in ['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']}
cols={'base':(.25,.31,.38),'lid':(.58,.67,.76),'dpad':(.94,.65,.22),'select':(.94,.65,.22),'back':(.94,.65,.22),'usb_c_collar':(.75,.78,.8),'usb_c_end_cap':(.36,.43,.51),'usb_a_end_cap':(.36,.43,.51)}
for mode in ['assembled','exploded','underside','contacts']:
 ren=vtk.vtkRenderer();ren.SetBackground(.95,.96,.98);w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(1600,850);w.AddRenderer(ren)
 for n,s in parts.items():
  if mode=='contacts' and n not in ['dpad','select','back']:continue
  if mode=='exploded' and 'end_cap' in n:s=s.translate((-9 if n=='usb_c_end_cap' else 9,0,0))
  if mode=='exploded':s=s.translate((0,0,12 if n=='lid' else 6 if n in ['dpad','select','back'] else 0))
  ren.AddActor(actor(s,cols[n]))
 cam=ren.GetActiveCamera();cam.ParallelProjectionOn();cam.SetPosition(100,-105,-95 if mode=='underside' else 105);cam.SetFocalPoint(36,-10,6 if mode=='exploded' else 0);cam.SetViewUp(0,0,-1 if mode=='underside' else 1);cam.SetParallelScale(45 if mode=='exploded' else 39);
 if mode=='contacts':
  cam.SetPosition(80,-60,-60);cam.SetFocalPoint(56,-10,4);cam.SetViewUp(0,0,-1);cam.SetParallelScale(15)
 ren.ResetCameraClippingRange();w.Render()
 f=vtk.vtkWindowToImageFilter();f.SetInput(w);f.Update();p=vtk.vtkPNGWriter();p.SetFileName(str(R/(mode+'.png')));p.SetInputConnection(f.GetOutputPort());p.Write();w.Finalize()
