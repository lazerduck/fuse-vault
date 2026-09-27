"""Render actual prototype solids with VTK; no generated/illustrative geometry."""
from pathlib import Path
import cadquery as cq
import vtk
import build_case as model

R=Path(__file__).resolve().parent/'prototype-a'
parts={n:cq.importers.importStep(str(R/(n+'.step'))).val() for n in ['base','lid','usb_c_collar','joystick_cap','back_plunger']}
refs,_=model.corrected_reference()
colours={'base':(.23,.29,.36),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'joystick_cap':(.18,.21,.25),'back_plunger':(.94,.59,.17)}
window=vtk.vtkRenderWindow();window.SetOffScreenRendering(1);window.SetSize(1800,1200);window.SetMultiSamples(8)

def actor(shape,colour,offset=(0,0,0)):
    points=vtk.vtkPoints();cells=vtk.vtkCellArray();verts,tris=shape.tessellate(.07,.15)
    for v in verts:points.InsertNextPoint(*v.toTuple())
    for tri in tris:
        cells.InsertNextCell(3)
        for i in tri:cells.InsertCellPoint(i)
    mesh=vtk.vtkPolyData();mesh.SetPoints(points);mesh.SetPolys(cells)
    mapper=vtk.vtkPolyDataMapper();mapper.SetInputData(mesh)
    a=vtk.vtkActor();a.SetMapper(mapper);a.SetPosition(*offset)
    a.GetProperty().SetColor(*colour);a.GetProperty().SetAmbient(.35);a.GetProperty().SetDiffuse(.65)
    return a

views=[('ASSEMBLED / PLA SHELL + TPU CAP',(0,.5,.5,1),'closed'),
       ('UNDERSIDE / ONE M2 SCREW',(.5,.5,1,1),'bottom'),
       ('BASE / COLLAR + PCB SUPPORTS',(0,0,.5,.5),'base'),
       ('EXPLODED / FIVE PRINTED PARTS',(.5,0,1,.5),'exploded')]
for title,viewport,mode in views:
    ren=vtk.vtkRenderer();ren.SetViewport(*viewport);ren.SetBackground(.95,.96,.975);window.AddRenderer(ren)
    for n,s in parts.items():
        if mode=='base' and n not in ['base','usb_c_collar']:continue
        offset=(0,0,0)
        if mode=='exploded':
            offset={'base':(0,0,-10),'lid':(0,0,23),'usb_c_collar':(-7,0,4),'joystick_cap':(0,0,31),'back_plunger':(0,0,30)}[n]
        ren.AddActor(actor(s,colours[n],offset))
    if mode in ['closed','bottom','exploded']:
        for n,s,c,side in refs:
            ren.AddActor(actor(s,c))
    camera=ren.GetActiveCamera()
    camera.SetFocalPoint(35,-9,10 if mode=='exploded' else 1)
    camera.SetPosition(-35,-110,100 if mode!='bottom' else -100)
    camera.SetViewUp(0,0,-1 if mode=='bottom' else 1)
    camera.ParallelProjectionOn();camera.SetParallelScale(46 if mode=='exploded' else 36)
    ren.ResetCameraClippingRange()
    t=vtk.vtkTextActor();t.SetInput(title);t.SetPosition(24,560);t.GetTextProperty().SetFontSize(21);t.GetTextProperty().SetColor(.12,.17,.23);ren.AddActor2D(t)
    t=vtk.vtkTextActor();t.SetInput('V2 fit prototype A  |  Physical fit not yet tested');t.SetPosition(24,18);t.GetTextProperty().SetFontSize(17);t.GetTextProperty().SetColor(.28,.32,.38);ren.AddActor2D(t)
window.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(window);capture.Update()
writer=vtk.vtkPNGWriter();writer.SetFileName(str(R/'preview.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();window.Finalize()
print(R/'preview.png')
