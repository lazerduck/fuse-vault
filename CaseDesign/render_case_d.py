"""Actual prototype D geometry, rendered from exported STEP files."""
from pathlib import Path
import cadquery as cq
import vtk
R=Path(__file__).resolve().parent/'prototype-d'
colours={'base':(.25,.31,.38),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'usb_c_end_cap':(.23,.38,.43),'usb_a_end_cap':(.23,.38,.43),'base_latch_coupon':(.25,.31,.38),'lid_latch_coupon':(.55,.64,.73)}
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in colours}
reference=cq.importers.importStep(str(R/'reference/reference.step')).val()
window=vtk.vtkRenderWindow();window.SetOffScreenRendering(1);window.SetSize(1800,1200);window.SetMultiSamples(8)
def actor(shape,colour):
    points=vtk.vtkPoints();cells=vtk.vtkCellArray();verts,tris=shape.tessellate(.05,.12)
    for v in verts:points.InsertNextPoint(*v.toTuple())
    for tri in tris:
        cells.InsertNextCell(3)
        for i in tri:cells.InsertCellPoint(i)
    mesh=vtk.vtkPolyData();mesh.SetPoints(points);mesh.SetPolys(cells)
    normals=vtk.vtkPolyDataNormals();normals.SetInputData(mesh);normals.SetFeatureAngle(35)
    mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(normals.GetOutputPort())
    a=vtk.vtkActor();a.SetMapper(mapper);a.GetProperty().SetColor(*colour);a.GetProperty().SetAmbient(.3);a.GetProperty().SetDiffuse(.7)
    return a
views=[('SLIM BODY / BARE JOYSTICK / BUTTON DIMPLE',(0,.5,.5,1),'closed'),('USB COVERS / SNAP RIB RETENTION',(.5,.5,1,1),'caps'),('LATCH TEST PAIR / PRESS TO CLOSE',(0,0,.5,.5),'latch'),('OPEN BASE / LOCATORS + LOCAL CABLE POCKET',(.5,0,1,.5),'base')]
for title,viewport,mode in views:
    ren=vtk.vtkRenderer();ren.SetViewport(*viewport);ren.SetBackground(.95,.96,.975);window.AddRenderer(ren)
    camera=ren.GetActiveCamera();camera.ParallelProjectionOn();camera.SetViewUp(0,0,1)
    if mode=='latch':
        ren.AddActor(actor(parts['base_latch_coupon'],colours['base']))
        ren.AddActor(actor(parts['lid_latch_coupon'].translate((0,0,5)),colours['lid']))
        camera.SetFocalPoint(67,-21,4);camera.SetPosition(81,-55,29);camera.SetParallelScale(15)
    else:
        for n,shape in parts.items():
            if n.endswith('coupon'):continue
            if mode=='base' and n not in ('base','usb_c_collar'):continue
            if mode=='closed' and n.endswith('end_cap'):continue
            ren.AddActor(actor(shape,colours[n]))
        if mode!='base':ren.AddActor(actor(reference,(.38,.58,.49)))
        camera.SetFocalPoint(36,-9,2);camera.SetPosition(-30,-105,105);camera.SetParallelScale(40)
    ren.ResetCameraClippingRange()
    for text,y,size in ((title,560,20),('V2 prototype D | Actual CAD | Physical testing pending',18,16)):
        t=vtk.vtkTextActor();t.SetInput(text);t.SetPosition(24,y);t.GetTextProperty().SetFontSize(size);t.GetTextProperty().SetColor(.12,.17,.23);ren.AddViewProp(t)
window.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(window);capture.Update()
writer=vtk.vtkPNGWriter();writer.SetFileName(str(R/'preview.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();window.Finalize()
