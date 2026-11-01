"""Render the exported style CAD, not generative concept art."""
from pathlib import Path
import json
import sys
import cadquery as cq
import vtk
ROOT=Path(__file__).resolve().parent

def actor(shape,color):
    vs,ts=shape.tessellate(.04)
    pts=vtk.vtkPoints(); cells=vtk.vtkCellArray()
    for v in vs: pts.InsertNextPoint(*v.toTuple())
    for tri in ts:
        cells.InsertNextCell(3)
        for i in tri: cells.InsertCellPoint(i)
    data=vtk.vtkPolyData(); data.SetPoints(pts); data.SetPolys(cells)
    mapper=vtk.vtkPolyDataMapper(); mapper.SetInputData(data)
    a=vtk.vtkActor(); a.SetMapper(mapper); a.GetProperty().SetColor(*color)
    a.GetProperty().SetAmbient(.25); a.GetProperty().SetDiffuse(.75)
    a.GetProperty().SetSpecular(.18); a.GetProperty().SetSpecularPower(35)
    return a

for style in (sys.argv[1:] or ['precision','panelled']):
    out=ROOT/('prototype-l-'+style)
    colors=json.loads((out/'validation.json').read_text())['colors']
    parts={n:cq.importers.importStep(str(out/(n+'.step'))).val() for n in colors}
    parts['display_reference']=cq.Workplane('XY').box(27.9,13.5,1.5,centered=False).translate((17.10146,-18.49208,4.1)).val()
    colors['display_reference']=(.04,.08,.10)
    for mode in ['assembled','top','exploded']:
        renderer=vtk.vtkRenderer(); renderer.SetBackground(.95,.96,.98)
        window=vtk.vtkRenderWindow(); window.SetOffScreenRendering(1)
        window.SetSize(1600,850); window.AddRenderer(renderer)
        for n,s in parts.items():
            if mode=='exploded':
                if 'end_cap' in n: s=s.translate((-9 if n=='usb_c_end_cap' else 9,0,0))
                s=s.translate((0,0,12 if n.startswith('lid') else 6 if n in ['dpad','select','back'] else 3 if n=='display_reference' else 0))
            renderer.AddActor(actor(s,colors[n]))
        cam=renderer.GetActiveCamera(); cam.ParallelProjectionOn()
        if mode=='top':
            cam.SetPosition(36,-10,150); cam.SetFocalPoint(36,-10,0); cam.SetViewUp(0,1,0); cam.SetParallelScale(33)
        else:
            cam.SetPosition(100,-105,105); cam.SetFocalPoint(36,-10,6 if mode=='exploded' else 0)
            cam.SetViewUp(0,0,1); cam.SetParallelScale(45 if mode=='exploded' else 39)
        renderer.ResetCameraClippingRange(); window.Render()
        capture=vtk.vtkWindowToImageFilter(); capture.SetInput(window); capture.Update()
        writer=vtk.vtkPNGWriter(); writer.SetFileName(str(out/(mode+'.png')))
        writer.SetInputConnection(capture.GetOutputPort()); writer.Write(); window.Finalize()
    print('Rendered',style,flush=True)
