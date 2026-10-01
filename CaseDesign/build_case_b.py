"""Prototype B: flat lid, broad cap, integral back flexure and TPU end cups.
Run with CadQuery 2.8. Prototype A remains unchanged.
"""
from pathlib import Path
import hashlib, itertools, json, math
import cadquery as cq
import vtk
import build_case as a
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'prototype-b'
OUT.mkdir(exist_ok=True)
a.OUT=OUT
P={'cap_radius':7.6,'cap_bottom':8.8,'cap_top':11.6,'tab_thickness':0.8,
   'back_rest_gap':0.25,'back_stop_gap':0.55,'end_cap_clearance':0.25,
   'end_cap_wall':1.4,'grip_interference':0.1}
box,cyl=a.box,a.cyl

def baseline_hashes():
    paths=list((ROOT/'prototype-a').rglob('*'))+[ROOT/'fuse-vault-v2-fit-prototype-a.zip']
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}

def rounded_x(x0,x1,y,z,w,h,r):
    return cq.Workplane('YZ',origin=(x0,y,z)).rect(w,h).extrude(x1-x0).edges('|X').fillet(r).val()

def cup(which):
    c=which=='c';lo,hi=(-14.657,3.5) if c else (69.5,91.65)
    ya,yb=-23.7,2.5 if c else 6.9
    clear=P['end_cap_clearance'];wall=P['end_cap_wall']
    yc=(ya+yb)/2;w=yb-ya+2*clear;h=13.8+2*clear
    outer=rounded_x(lo,hi,yc,1.3,w+2*wall,h+2*wall,1.6)
    outer=cq.Workplane(obj=outer).faces('<X' if c else '>X').edges().fillet(.6).val()
    cavity=rounded_x(lo+wall if c else lo-1,hi+1 if c else hi-wall,yc,1.3,w,h,.4)
    hard=outer.cut(cavity)
    # Seating ledges bear on shell faces, leaving the connector untouched.
    s0,s1=(-3.65,-2.65) if c else (73.75,74.75)
    seat=s1 if c else s0
    ramp_start=seat-1.6 if c else seat+1.6
    for wall_z,tip_z in ((8.5,6.9),(-5.95,-4.6)):
        ramp=cq.Workplane('XZ',origin=(0,-3,0)).polyline([(ramp_start,wall_z),(seat,tip_z),(seat,wall_z)]).close().extrude(16).val()
        hard=hard.fuse(ramp)
    hard=hard.clean()
    r0,r1=(.2,2.6) if c else (70,71.4)
    ribs=[]
    for y in (ya-.5,yb+.5):
        ribs.append(cq.Workplane('YZ',origin=(r0,y,1)).circle(.6).extrude(r1-r0).val())
    return hard.fuse(*ribs).clean(),hard

def build():
    parts,refs,keeps=a.build()
    old_collar=parts['usb_c_collar']
    base=parts['base'].cut(box(45.7,52.6,-23.1,-12.3,4.4,5.8))
    lid=parts['lid'].cut(cyl(49.149,-15.875,3.6,5.8,6.6))
    lid=lid.fuse(cyl(49.149,-15.875,3.5,6.6,8.2))
    shell=a.shell_blank(4.4,8.2,'>Z').cut(box(-.6,71.7,-21.85,.7,-4,6.6))
    lid=lid.fuse(shell.intersect(box(47.899,50.399,-23.1,-21.45,4.4,5.9)))
    # Rounded U slot; roof top remains flat and prints against the bed.
    for x in (46.824,51.474):
        slot=cq.Workplane('XY',origin=(x,-12.1375,6.5)).slot2D(13.325,.65,90).extrude(2).val()
        lid=lid.cut(slot)
    lid=lid.cut(box(46.824,51.474,-18.8,-18.15,6.5,8.3))
    # Long leaf, with a ramp back into the full-thickness roof at its root.
    lid=lid.cut(box(47.149,51.149,-18.15,-7.3,6.5,7.4))
    ramp=cq.Workplane('YZ',origin=(47.149,0,0)).polyline([(-7.3,6.5),(-5.8,6.5),(-5.8,6.6),(-7.3,7.4)]).close().extrude(4).val()
    lid=lid.cut(ramp).fuse(cyl(49.149,-15.875,1.2,5.65,7.45))
    # Short ledges beneath the leaf replace both delicate rings.
    for x0,x1 in ((45.5,47.649),(50.649,52.8)):
        lid=lid.fuse(box(x0,x1,-17.3,-14.5,6.05,6.85))
    lid=lid.clean()
    jx,jy=60.325,-14.732
    cap=cyl(jx,jy,P['cap_radius'],P['cap_bottom'],P['cap_top'])
    cap=cq.Workplane(obj=cap).faces('>Z').edges().fillet(.8).faces('<Z').edges().fillet(.4).val()
    cap=cap.cut(box(jx-1.3,jx+1.3,jy-1.3,jy+1.3,8.7,10.85))
    lead=cq.Workplane('XY',origin=(jx,jy,8.79)).rect(3.1,3.1).workplane(offset=.3).rect(2.6,2.6).loft().val()
    cap=cap.cut(lead)
    for dx,dy in ((4.8,0),(-4.8,0),(0,4.8),(0,-4.8)):
        cap=cap.cut(cyl(jx+dx,jy+dy,1,11.4,11.8))
    parts={'base':base.clean(),'lid':lid,'usb_c_collar':old_collar,'joystick_cap':cap.clean()}
    hard={}
    for end in ('c','a'):
        parts['usb_'+end+'_end_cap'],hard[end]=cup(end)
    coupon=lid.intersect(box(44.6,53.6,-20,-3.5,5.5,8.3)).clean()
    return parts,refs,keeps,hard,coupon

def export_check(parts,refs,keeps,hard,coupon,revision="B"):
    checks={};fail=[];grips=[]
    def collision(n,s,m,t):
        v=s.intersect(t).Volume()
        if v>1e-5:fail.append({'a':n,'b':m,'volume_mm3':v})
    for n,s in {**parts,'back_button_coupon':coupon}.items():
        assert s.isValid() and len(s.Solids())==1,(n,'invalid/disconnected')
        cq.exporters.export(s,str(OUT/(n+'.step')))
        rot=((0,1,0),90) if n in ('usb_c_collar','usb_a_end_cap') else (((0,1,0),-90) if n=='usb_c_end_cap' else (((1,0,0),180) if n in ('lid','joystick_cap','back_button_coupon') else None))
        cq.exporters.export(a.on_bed(s,rot),str(OUT/(n+'.stl')),tolerance=.025,angularTolerance=.12)
        reload=cq.importers.importStep(str(OUT/(n+'.step'))).val()
        assert reload.isValid() and abs(reload.Volume()-s.Volume())<1e-5
        mesh=vtk.vtkSTLReader();mesh.SetFileName(str(OUT/(n+'.stl')));mesh.MergingOn();mesh.Update()
        edges=vtk.vtkFeatureEdges();edges.SetInputConnection(mesh.GetOutputPort());edges.BoundaryEdgesOn();edges.NonManifoldEdgesOn();edges.FeatureEdgesOff();edges.ManifoldEdgesOff();edges.Update()
        assert edges.GetOutput().GetNumberOfCells()==0,(n,'open mesh')
        assert abs(mesh.GetOutput().GetBounds()[4])<1e-5
        b=s.BoundingBox();checks[n]={'valid_single_solid':True,'step_round_trip':True,'stl_closed_manifold':True,'stl_on_bed':True,'dimensions_mm':[b.xlen,b.ylen,b.zlen]}
    core={n:s for n,s in parts.items() if not n.endswith('end_cap')}
    for (n,s),(m,t) in itertools.combinations(core.items(),2):collision(n,s,m,t)
    for n,s in parts.items():
        for m,t,_,_ in refs:collision(n,s,m,t)
        for m,t,_,_ in keeps:
            if m.startswith(('solder_','locating_peg_')) or m.endswith('_terminal_envelope') or m=='FPC_route_provisional':collision(n,s,m,t)
    for end,s in hard.items():
        for n,t in core.items():
            collision('usb_'+end+'_end_cap_without_grips',s,n,t)
            v=parts['usb_'+end+'_end_cap'].intersect(t).Volume()
            if v>1e-5:
                if n not in ('base','lid'):fail.append({'a':end,'b':n,'volume_mm3':v})
                else:grips.append({'end':end,'case_part':n,'volume_mm3':v,'intended_TPU_interference_mm':.1})
    closure=[]
    for angle in (0,2,5,10,15):
        s=parts['lid'].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
        targets={'base':parts['base'],'collar':parts['usb_c_collar'],'reference':cq.Compound.makeCompound([t for _,t,_,_ in refs])}
        row={'angle_degrees':angle}
        for n,t in targets.items():
            row[n+'_intersection_mm3']=s.intersect(t).Volume();collision('tilted_lid_'+str(angle),s,n,t)
        closure.append(row)
    for angle in range(0,360,45):
        r=math.radians(angle);s=parts['joystick_cap'].translate((math.cos(r),math.sin(r),-.5))
        for n,t in parts.items():
            if n!='joystick_cap':collision('cap_movement_'+str(angle),s,n,t)
    for end,s in hard.items():
        for distance in (5,10,20):
            moved=s.translate((distance*(-1 if end=='c' else 1),0,0))
            for n,t in core.items():collision('cap_removal_'+end+str(distance),moved,n,t)
            for n,t,_,_ in refs:collision('cap_removal_'+end+str(distance),moved,n,t)
    # Compare the actual socket void within its unchanged engagement region.
    old_cap=cq.importers.importStep(str(ROOT/'prototype-a/joystick_cap.step')).val()
    socket_region=box(58.725,61.925,-16.332,-13.132,8.79,10.85)
    old_socket=socket_region.cut(old_cap)
    new_socket=socket_region.cut(parts['joystick_cap'])
    socket_difference=old_socket.cut(new_socket).Volume()+new_socket.cut(old_socket).Volume()
    assert socket_difference<1e-5
    old=cq.importers.importStep(str(ROOT/'prototype-a/usb_c_collar.step')).val()
    collar_difference=old.cut(parts['usb_c_collar']).Volume()+parts['usb_c_collar'].cut(old).Volume()
    assert collar_difference<1e-5
    assy=cq.Assembly(name='V2_prototype_'+revision)
    colours={'base':(.23,.29,.36),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'joystick_cap':(.16,.2,.25),'usb_c_end_cap':(.28,.37,.43),'usb_a_end_cap':(.28,.37,.43)}
    for n,s in parts.items():assy.add(s,name=n,color=cq.Color(*colours[n]))
    assy.export(str(OUT/'case_assembly.step'))
    for n,s,c,_ in refs:assy.add(s,name='REF_'+n,color=cq.Color(*c))
    assy.export(str(OUT/'case_with_board.step'))
    report={'revision':revision,'parts':checks,'collisions':fail,'intended_grip_interference':grips,'sampled_lid_closure':closure,'collar_change_volume_mm3':collar_difference,'socket_change_volume_mm3':socket_difference,'cap_proud_mm':3.4,'back_rest_gap_mm':.25,'back_stop_gap_mm':.55,'limitations':['Physical B print and populated V2 board fit pending.','Flexure stiffness/fatigue, TPU grip force and actual SW4 actuation not established.','Sampled movement is not a continuous kinematic or tolerance proof.','Joystick remains exposed; wider cap is not a protective guard.','Soft caps are protective covers, not crush-proof or sealed.']}
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'parameters_used.json').write_text(json.dumps(P,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    assert not fail,'Inspect collisions in validation.json'

if __name__=='__main__':
    before=baseline_hashes()
    parts,refs,keeps,hard,coupon=build()
    export_check(parts,refs,keeps,hard,coupon)
    assert before==baseline_hashes(),'Prototype A changed'
    (OUT/'prototype_a_preservation.json').write_text(json.dumps(before,indent=2)+'\n')
