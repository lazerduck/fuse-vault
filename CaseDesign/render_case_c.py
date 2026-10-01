"""Render real C STEP geometry, including a populated-board loading view."""
from pathlib import Path
import cadquery as cq
import vtk
R=Path(__file__).resolve().parent/'prototype-c'
colours={'base':(.25,.31,.38),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'joystick_cap':(.19,.24,.3)}
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in colours}
reference=cq.importers.importStep(str(R/'reference/reference.step')).val()
hardware=cq.importers.importStep(str(R/'case_with_hardware.step')).val()
# Hardware dimensions are the same nominal envelopes used by build_case_c.py.
from build_case_c import cyl,hexagon,box
nut=hexagon(4,6.4,8).cut(cyl(68,3.4,1.1,6.3,8.1))
screw=cyl(68,3.4,1,-2,8).fuse(cyl(68,3.4,2,-4,-2))
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
views=[('ASSEMBLED / NUT ACCESSIBLE FROM TOP',(0,.5,.5,1),'closed'),('LOWER BOARD + SD CARD + COLLAR INTO BASE',(.5,.5,1,1),'loading'),('USB-C / OPEN-TOP COLLAR SEAT',(0,0,.5,.5),'collar'),('TOP NUT / BOLT FROM BELOW (EXPLODED)',(.5,0,1,.5),'nut')]
for title,viewport,mode in views:
    ren=vtk.vtkRenderer();ren.SetViewport(*viewport);ren.SetBackground(.95,.96,.975);window.AddRenderer(ren)
    camera=ren.GetActiveCamera();camera.ParallelProjectionOn();camera.SetViewUp(0,0,1)
    if mode=='closed':
        for n,s in parts.items():ren.AddActor(actor(s,colours[n]))
        ren.AddActor(actor(reference,(.35,.55,.48)));ren.AddActor(actor(nut,(.78,.79,.81)))
        camera.SetFocalPoint(36,-9,3);camera.SetPosition(-30,-105,105);camera.SetParallelScale(39)
    elif mode=='loading':
        ren.AddActor(actor(parts['base'],colours['base']))
        ren.AddActor(actor(reference.translate((0,0,13)),(.38,.62,.51)))
        ren.AddActor(actor(parts['usb_c_collar'].translate((0,0,13)),colours['usb_c_collar']))
        camera.SetFocalPoint(36,-9,9);camera.SetPosition(-30,-105,105);camera.SetParallelScale(39)
    elif mode=='collar':
        crop=box(-6,9,-27,5,-6,6)
        ren.AddActor(actor(parts['base'].intersect(crop),colours['base']))
        ren.AddActor(actor(parts['usb_c_collar'].translate((0,0,8)),colours['usb_c_collar']))
        camera.SetFocalPoint(0,-10,3);camera.SetPosition(-40,-60,50);camera.SetParallelScale(18)
    else:
        crop=box(63,76,-1,8,-6,9)
        for n in ('base','lid'):ren.AddActor(actor(parts[n].intersect(crop),colours[n]))
        ren.AddActor(actor(nut.translate((0,0,4)),(.72,.74,.78)))
        ren.AddActor(actor(screw.translate((0,0,-12)),(.72,.74,.78)))
        camera.SetFocalPoint(68,3.4,-1);camera.SetPosition(85,38,23);camera.SetParallelScale(17)
    ren.ResetCameraClippingRange()
    for text,y,size in ((title,560,20),('V2 prototype C | Actual CAD geometry | Physical fit pending',18,16)):
        t=vtk.vtkTextActor();t.SetInput(text);t.SetPosition(24,y);t.GetTextProperty().SetFontSize(size);t.GetTextProperty().SetColor(.12,.17,.23);ren.AddViewProp(t)
window.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(window);capture.Update()
writer=vtk.vtkPNGWriter();writer.SetFileName(str(R/'preview.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();window.Finalize()
