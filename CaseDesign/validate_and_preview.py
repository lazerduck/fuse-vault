"""Geometric QA, STEP round trip and source-render registration plots."""
from pathlib import Path
import json, math
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from PIL import Image
import cadquery as cq
import build_reference as model
R=Path(__file__).resolve().parent
parts,keeps=model.build()
G=model.G;P=model.P
# Read clean B-rep geometry: STL tessellation adds conservative mesh bounds.
board=cq.importers.importStep(str(R/'pcb.step')).val(); bb=board.BoundingBox()
outer=G['outline'];xmin=min(p[0] for p in outer);xmax=max(p[0] for p in outer);ymin=min(p[1] for p in outer);ymax=max(p[1] for p in outer)
area=(xmax-xmin)*(ymax-ymin)
for loop in G['cutouts']:
    area-=(max(x for x,y in loop)-min(x for x,y in loop))*(max(y for x,y in loop)-min(y for x,y in loop))
for h in G['drills']:
    area-=math.pi*(h['diameter']/2)**2+math.hypot(h['end_x']-h['x'],h['end_y']-h['y'])*h['diameter']
expected_volume=area*P['pcb_thickness']
assert abs(board.Volume()-expected_volume)<1e-6,(board.Volume(),expected_volume)
assert len(board.Solids())==1
assert abs(bb.xlen-73.335)<1e-6 and abs(bb.ylen-21.237)<1e-6
assert abs(bb.zlen-P['pcb_thickness'])<1e-6
for h in G['drills']:
    assert not board.isInside(cq.Vector(h['x'],h['y'],P['pcb_thickness']/2),1e-7)
check={}
for name,items in [('reference',parts),('keepouts',keeps)]:
    loaded=cq.importers.importStep(str(R/(name+'.step'))).val()
    solids=loaded.Solids()
    assert len(solids)==len(items),(name,len(solids),len(items))
    assert all(s.isValid() for s in solids)
    before=sum(sh.Volume() for _,sh,_,_ in items)
    after=sum(s.Volume() for s in solids)
    assert abs(before-after)<1e-5
    check[name]=dict(solids=len(solids),valid=True,volume_before=before,volume_after=after)

# Register using PCB boundaries, then evaluate independent visible landmarks.
# Pixels are manually read to about 1 px; this validates XY, never Z.
scale_x=721/73.335;scale_y=209/21.237
landmarks={'top':{'SW3':(711,393),'SW4':(601,405),'L1':(174,423)},'bottom':{'SW1':(809,281),'SW2':(808,421),'U2':(477,325),'L2':(619,277)}}
positions={r['ref']:r for r in G['placements']}
residuals=[]
for side,lms in landmarks.items():
    for ref,observed in lms.items():
        r=positions[ref]
        px=116+(r['x']-xmin)*scale_x if side=='top' else 876-(r['x']-xmin)*scale_x
        py=248-r['y']*scale_y
        err=math.hypot(px-observed[0],py-observed[1])
        residuals.append(dict(side=side,ref=ref,observed_pixel=observed,predicted_pixel=[px,py],error_px=err,error_mm=err/scale_x))
assert max(r['error_px'] for r in residuals)<3

plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10})
for side,letter in [('top','T'),('bottom','B')]:
    fig,ax=plt.subplots(figsize=(16,5),facecolor='#101925')
    ax.set_facecolor('#101925')
    im=Image.open(R/'source'/(side+'.png'))
    ax.imshow(im)
    def xy(x,y):return (116+(x-xmin)*scale_x if side=='top' else 876-(x-xmin)*scale_x,248-y*scale_y)
    def rect(x0,y0,x1,y1,color,lw=1.2):
        p=xy(x0,y0);q=xy(x1,y1)
        ax.add_patch(Rectangle((min(p[0],q[0]),min(p[1],q[1])),abs(q[0]-p[0]),abs(q[1]-p[1]),fill=False,edgecolor=color,lw=lw))
    rect(xmin,ymin,xmax,ymax,'#23e6ff',2)
    for loop in G['cutouts']:rect(min(x for x,y in loop),min(y for x,y in loop),max(x for x,y in loop),max(y for x,y in loop),'#23e6ff',2)
    for name,sh,col,s in parts[1:]:
        if s!=letter and name not in ('J1','J2'):continue
        b=sh.BoundingBox()
        if name in P['special_components'] or name=='TFT_PLACEHOLDER':
            rect(b.xmin,b.ymin,b.xmax,b.ymax,'#ffbd5b')
            px,py=xy((b.xmin+b.xmax)/2,(b.ymin+b.ymax)/2)
            ax.text(px,py-8,name.replace('_PLACEHOLDER',' guide'),ha='center',va='bottom',fontsize=8,color='#ffffff',bbox=dict(facecolor='#101925',alpha=.8,edgecolor='none',pad=2))
    for ref,r in positions.items():
        if r['side']==letter:
            px,py=xy(r['x'],r['y']);ax.plot(px,py,'+',color='#ff59df',ms=5,mew=.8)
    for h in G['drills']:
        if h['diameter']<.5:continue
        px,py=xy(h['x'],h['y']);ax.add_patch(Circle((px,py),h['diameter']/2*scale_x,fill=False,edgecolor='#23e6ff',lw=.8))
    ax.set_xlim(0,990);ax.set_ylim(490,210);ax.axis('off')
    fig.text(.03,.94,'FUSE VAULT  /  '+side.upper()+' REGISTRATION',color='white',fontsize=15)
    fig.text(.03,.88,'Cyan: board / cutout / drills   |   Pink: CPL origins   |   Amber: model envelopes (some assumed)',color='white',fontsize=11)
    fig.subplots_adjust(left=.02,right=.99,bottom=.02,top=.82)
    fig.savefig(R/(side+'_validation.png'),dpi=160,facecolor=fig.get_facecolor());plt.close(fig)

# A depth-buffered renderer is needed: painter-sorted board meshes can hide parts.
import vtk
window=vtk.vtkRenderWindow();window.SetOffScreenRendering(1);window.SetSize(1800,1000);window.SetMultiSamples(8)
for top in [True,False]:
    renderer=vtk.vtkRenderer();renderer.SetBackground(.93,.95,.97)
    renderer.SetViewport(0,.5 if top else 0,1,1 if top else .5);window.AddRenderer(renderer)
    for name,sh,color,side in parts:
        verts,tris=sh.tessellate(.04,.15)
        points=vtk.vtkPoints()
        for v in verts:points.InsertNextPoint(*v.toTuple())
        cells=vtk.vtkCellArray()
        for tri in tris:
            cells.InsertNextCell(3)
            for idx in tri:cells.InsertCellPoint(idx)
        mesh=vtk.vtkPolyData();mesh.SetPoints(points);mesh.SetPolys(cells)
        mapper=vtk.vtkPolyDataMapper();mapper.SetInputData(mesh)
        actor=vtk.vtkActor();actor.SetMapper(mapper);actor.GetProperty().SetColor(*color)
        actor.GetProperty().SetAmbient(.3);actor.GetProperty().SetDiffuse(.7)
        renderer.AddActor(actor)
    camera=renderer.GetActiveCamera();camera.SetFocalPoint(38,-10,2)
    camera.SetPosition(50,-100,100 if top else -100);camera.SetViewUp(0,0,1 if top else -1)
    camera.ParallelProjectionOn();camera.SetParallelScale(17);renderer.ResetCameraClippingRange()
    text=vtk.vtkTextActor();text.SetInput('FUSE VAULT / '+('TOP - provisional TFT volume' if top else 'UNDERSIDE - microSD, FPC and switches'))
    text.SetPosition(50,440);text.GetTextProperty().SetFontSize(26);text.GetTextProperty().SetColor(.07,.12,.18)
    renderer.AddActor2D(text)
    note=vtk.vtkTextActor();note.SetInput('73.335 x 21.237 x 1.6 mm PCB  |  Simplified component envelopes; see assumptions')
    note.SetPosition(50,20);note.GetTextProperty().SetFontSize(18);note.GetTextProperty().SetColor(.2,.26,.3);renderer.AddActor2D(note)
window.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(window);capture.Update()
writer=vtk.vtkPNGWriter();writer.SetFileName(str(R/'reference_preview.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();window.Finalize()

report=dict(board_size_mm=[bb.xlen,bb.ylen,bb.zlen],board_valid=board.isValid(),board_solids=len(board.Solids()),board_volume_mm3=board.Volume(),independent_expected_volume_mm3=expected_volume,unique_drill_features=len(G['drills']),round_holes=sum(h['kind']=='round' for h in G['drills']),slots=sum(h['kind']=='slot' for h in G['drills']),cpl_rows=len(G['placements']),step_round_trip=check,render_registration_landmarks=residuals,max_landmark_error_px=max(r['error_px'] for r in residuals),limitations=['Render checks validate XY only; no Z measurements available.','Component envelope dimensions and offsets have mixed evidence; see parameters.json.','TFT geometry and flex route are provisional.','Reference STL contains separate overlapping solids and is for viewing, not printing a finished assembly.'])
(R/'validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ('render_registration_landmarks','limitations')},indent=2))
