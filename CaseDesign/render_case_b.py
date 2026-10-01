"""Render exported B geometry directly; no mutation of model or reference files."""
from pathlib import Path
import cadquery as cq
import vtk
R=Path(__file__).resolve().parent/'prototype-b'
colours={'base':(.23,.29,.36),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'joystick_cap':(.16,.2,.25),'usb_c_end_cap':(.28,.37,.43),'usb_a_end_cap':(.28,.37,.43),'back_button_coupon':(.55,.64,.73)}
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in colours}
window=vtk.vtkRenderWindow();window.SetOffScreenRendering(1);window.SetSize(1800,1200);window.SetMultiSamples(8)
def actor(shape,colour):
    points=vtk.vtkPoints();cells=vtk.vtkCellArray();verts,tris=shape.tessellate(.05,.12)
    for v in verts:points.InsertNextPoint(*v.toTuple())
    for tri in tris:
        cells.InsertNextCell(3)
        for i in tri:cells.InsertCellPoint(i)
    mesh=vtk.vtkPolyData();mesh.SetPoints(points);mesh.SetPolys(cells)
    normals=vtk.vtkPolyDataNormals();normals.SetInputData(mesh);normals.SetFeatureAngle(35);normals.Update()
    mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(normals.GetOutputPort())
    a=vtk.vtkActor();a.SetMapper(mapper);a.GetProperty().SetColor(*colour);a.GetProperty().SetAmbient(.3);a.GetProperty().SetDiffuse(.7)
    return a
views=[('FLAT LID / WIDER, LOWER D-PAD',(0,.5,.5,1),'open'),('BOTH USB ENDS COVERED',(.5,.5,1,1),'capped'),('BACK BUTTON / UNDERSIDE TEST COUPON',(0,0,.5,.5),'coupon'),('LID UNDERSIDE / INTEGRAL FLEXURE',(.5,0,1,.5),'lid')]
for title,viewport,mode in views:
    ren=vtk.vtkRenderer();ren.SetViewport(*viewport);ren.SetBackground(.95,.96,.975);window.AddRenderer(ren)
    if mode=='coupon':selected={'back_button_coupon':parts['back_button_coupon']}
    elif mode=='lid':selected={'lid':parts['lid']}
    else:selected={n:s for n,s in parts.items() if n!='back_button_coupon' and (mode=='capped' or not n.endswith('end_cap'))}
    for n,s in selected.items():ren.AddActor(actor(s,colours[n]))
    camera=ren.GetActiveCamera();camera.ParallelProjectionOn()
    if mode=='coupon':camera.SetFocalPoint(49,-12,7);camera.SetPosition(38,-37,-23);camera.SetViewUp(0,0,-1);camera.SetParallelScale(12)
    else:
        camera.SetFocalPoint(36,-9,3);camera.SetPosition(-30,-105,-100 if mode=='lid' else 105);camera.SetViewUp(0,0,-1 if mode=='lid' else 1);camera.SetParallelScale(39)
    ren.ResetCameraClippingRange()
    for text,y,size in ((title,560,21),('V2 prototype B  |  Actual CAD geometry / print testing pending',18,16)):
        t=vtk.vtkTextActor();t.SetInput(text);t.SetPosition(24,y);t.GetTextProperty().SetFontSize(size);t.GetTextProperty().SetColor(.12,.17,.23);ren.AddActor2D(t)
window.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(window);capture.Update()
writer=vtk.vtkPNGWriter();writer.SetFileName(str(R/'preview.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();window.Finalize()
