"""Build the V2 fit prototype. CadQuery 2.8; run from any directory.

Outputs go to prototype-a/. Original reference exports are preserved. All STL
parts are individually oriented on Z=0. STEP parts retain assembly coordinates.
"""
from pathlib import Path
import csv
import json
import math
import itertools
import cadquery as cq
import build_reference as ref

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'prototype-a'
P = json.loads((ROOT / 'prototype_parameters.json').read_text())
OUT.mkdir(exist_ok=True)


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane('XY').box(x1-x0, y1-y0, z1-z0, centered=False).translate((x0,y0,z0)).val()


def cyl(x,y,r,z0,z1):
    return cq.Workplane('XY').center(x,y).circle(r).extrude(z1-z0).translate((0,0,z0)).val()


def capsule_x(x0,x1,width,height,y=-10.668,z=1.85):
    # Rounded USB shell cross section, extruded along board X.
    return cq.Workplane('YZ',origin=(x0,y,z)).slot2D(width,height).extrude(x1-x0).val()


def shell_blank(z0,z1,chamfer_end=None):
    w=cq.Workplane('XY').polyline(P['outline_xy']).close().extrude(z1-z0)
    w=w.edges('|Z').fillet(P['outline_corner_radius'])
    if chamfer_end:
        w=w.faces(chamfer_end).edges().chamfer(P['outer_edge_chamfer'])
    return w.translate((0,0,z0)).val()


def corrected_reference():
    # Isolated outputs prevent old validation reports being mistaken for new QA.
    ref.ROOT=OUT/'reference'
    ref.ROOT.mkdir(exist_ok=True)
    ref.P['special_components']['J2']['size']=[13.61,8.75,2.4]
    ref.P['special_components']['J2']['bottom_from_surface']=-0.95
    ref.P['special_components']['J2']['evidence']='XKB drawing: 2.40 +/-0.03 shell, contact plane 0.95 above bottom; installed seating still provisional.'
    ref.P['display']['thickness']=1.6
    ref.P['special_components']['SW3']['body_height']=2.3
    ref.P['special_components']['SW3']['evidence']='Supplied 10x10x9-6P WX drawing: 9 +/-0.1 overall, 2.3 +/-0.1 body, 2.5 square shaft. Secondary shoulder geometry simplified.'
    ref.P['display']['evidence']='Manufacturer drawing: 27.9 x 13.5 x 1.5 +/-0.1; 1.6 maximum envelope. XY guide, 0.5 mounting gap and flex orientation provisional.'
    ref.P['special_components']['J1']['size'][1]=12.67
    ref.P['special_components']['J1']['evidence']='AM90 drawing: shell 4.5 +/-0.1, rear width 12.52 +/-0.15; seating remains provisional. Conservative rectangular rear width.'
    parts,keeps=ref.build()
    parts=list(parts)
    # Main USB-C shell + broader rear attachment, not a single enclosing box.
    shell=capsule_x(-11.757,-0.507,8.25,2.4)
    rear=box(-0.507,1.853,-10.668-8.75/2,-10.668+8.75/2,0.65,3.05)
    parts=[(n,shell.fuse(rear).clean() if n=='J2' else s,c,side) for n,s,c,side in parts]
    joystick=box(55.325,65.325,-19.982,-9.482,1.6,3.9)
    joystick=joystick.fuse(cyl(60.325,-14.732,1.875,3.9,6.6),box(59.075,61.575,-15.982,-13.482,6.6,10.6))
    parts=[(n,joystick if n=='SW3' else s,c,side) for n,s,c,side in parts]
    # Latched card is a provisional envelope; no external extraction corridor.
    parts.append(('SD_CARD_ASSUMED',box(51.904,62.904,-13.8,1.2,-1.65,-0.65),(.2,.22,.25),'B'))
    # No imposed flex minimum bend radius: keep the original conservative corridor.
    keeps=[k for k in keeps if k[0] not in ('microSD_withdrawal_access','J2_clearance')]
    keeps.append(('J2_clearance',capsule_x(-12.0,2.1,9.25,3.4),(.9,.6,.2),'T'))
    keeps.append(('SD_installed_clearance',box(51.5,63.3,-14.2,1.6,-2.05,-.25),(.9,.6,.2),'B'))
    (ref.ROOT/'parameters_used.json').write_text(json.dumps(ref.P,indent=2)+'\n')
    # Overwrite the new assembly with the shaped connector/card; original untouched.
    assy=cq.Assembly(name='V2_corrected_fit_reference')
    for n,s,c,side in parts: assy.add(s,name=n,color=cq.Color(*c))
    assy.export(str(ref.ROOT/'reference.step'))
    cq.exporters.export(cq.Compound.makeCompound([s for _,s,_,_ in parts]),str(ref.ROOT/'reference.stl'))
    clearance=cq.Assembly(name='V2_corrected_fit_keepouts')
    for n,s,c,side in keeps: clearance.add(s,name=n,color=cq.Color(*c))
    clearance.export(str(ref.ROOT/'keepouts.step'))
    with (ref.ROOT/'component_envelopes.csv').open('w',newline='') as f:
        writer=csv.writer(f);writer.writerow(['name','side','xmin','xmax','ymin','ymax','zmin','zmax'])
        for n,s,c,side in parts:
            b=s.BoundingBox();writer.writerow([n,side]+[getattr(b,k) for k in ['xmin','xmax','ymin','ymax','zmin','zmax']])
    return parts,keeps


def build():
    refs,keeps=corrected_reference()
    z0,zf,zs,zc,zt=[P[k] for k in ['base_bottom','floor_top','seam_z','lid_ceiling','lid_top']]
    base=shell_blank(z0,zs,'<Z')
    lid=shell_blank(zs,zt,'>Z')
    pocket=box(-.6,71.7,-21.85,.7,zf,zc)
    base=base.cut(pocket)
    lid=lid.cut(pocket)
    # PCB edge clearance; corner portions may nest into the end wall.
    pcb_room=box(-.477,73.558,-21.587,.35,-.25,1.85)
    base=base.cut(pcb_room)
    # Near-USB-C reinforcement joins the end wall and holds the collar flange.
    base=base.fuse(box(-2.5,.7,-18.3,-3.0,zf,zs))
    lid=lid.fuse(box(-2.5,.7,-18.3,-3.0,zs,zc+.01))
    base=base.cut(pcb_room)
    # Collar, 0.2 mm shell clearance per side; flange captured in a split pocket.
    c=P['collar_bore_clearance_per_side']
    front,end,flange=[P[k] for k in ['collar_front_x','collar_rear_x','collar_flange_front_x']]
    collar=capsule_x(front,end,11.45,5.6).fuse(capsule_x(flange,end,14.2,7.4))
    collar=collar.cut(capsule_x(front-.1,end+.1,8.25+2*c,2.4+2*c)).clean()
    gap=P['collar_capture_clearance']
    collar_room=capsule_x(front-.5,flange-gap,11.45+2*gap,5.6+2*gap)
    collar_room=collar_room.fuse(capsule_x(flange-gap,end+gap,14.2+2*gap,7.4+2*gap))
    connector_room=capsule_x(-15,2.3,9.35,3.4)
    connector_room=connector_room.fuse(box(-.8,2.4,-15.343,-5.993,.15,3.55))
    # Port spans are purposely open; exposure is documented, not certified.
    usb_a_room=box(69.4,95,-17.8,-4.3,1.05,6.75)
    for cutter in [collar_room,connector_room,usb_a_room]:
        base=base.cut(cutter); lid=lid.cut(cutter)
    # Single screw in the local shoulder, outside the board.
    sx,sy=P['screw_xy'];r=P['screw_post_diameter']/2
    base=base.fuse(cyl(sx,sy,r,zf,zs))
    lid=lid.fuse(cyl(sx,sy,r,zs+.15,zc+.1))
    base=base.cut(cyl(sx,sy,P['screw_clearance']/2,z0-1,zs+1))
    base=base.cut(cyl(sx,sy,P['screw_head_diameter']/2,z0-1,P['screw_head_seat_z']))
    af=P['nut_pocket_across_flats']
    nut=cq.Workplane('XY').center(sx,sy).polygon(6,af/math.cos(math.pi/6)).extrude(P['nut_pocket_roof_z']-zs+.1).translate((0,0,zs-.1)).val()
    lid=lid.cut(nut).cut(cyl(sx,sy,1.2,zs-.1,7.55))
    # Six paired supports: base contacts PCB underside; lid allows 0.2 mm above.
    for x,y in P['support_xy']:
        if y>-10: ya,yb=-.55,1.15
        else: ya,yb=-22.2,-20.7
        base=base.fuse(box(x-1,x+1,ya,yb,zf,0))
        lid=lid.fuse(box(x-1,x+1,ya,yb,1.8,zc+.05))
        base=base.cut(box(x-1.25,x+1.25,ya-.2,yb+.2,1.6,zs+.1))
    # Non-flexing locating tongues at USB-C end; lid is offered at an angle.
    for y in [-.9,-20.8]:
        socket=box(-1.9,2.0,y-1.5,y+1.5,2.05,3.6)
        base=base.cut(socket)
        base=base.cut(box(-.35,1.95,y-1.4,y+1.4,3.4,zs+.1))
        neck=box(.1,1.7,y-1.15,y+1.15,2.45,zc+.05)
        tongue=box(-1.55,1.7,y-1.15,y+1.15,2.45,3.05)
        lid=lid.fuse(neck,tongue)
    # Two short seam keys avoid long friction fits and still align the halves.
    for ya,yb in [(.8,1.45),(-22.6,-21.95)]:
        base=base.fuse(box(29,39,ya,yb,zs-.05,zs+.8))
        lid=lid.cut(box(28.7,39.3,ya-.25,yb+.25,zs-.1,zs+1.05))
    # Screen window: long axis offset toward +X, flex exits toward PCB cutout.
    screen_center=(32.45146,-11.74208)
    aperture=cq.Workplane('XY').center(*screen_center).rect(22.5,11.6).extrude(zt-zc+2).translate((0,0,zc-1)).val()
    lid=lid.cut(aperture)
    # A shallow opening bevel, made as a loft so it survives topology changes.
    bevel=cq.Workplane('XY',origin=(*screen_center,zt-.45)).rect(22.5,11.6).workplane(offset=.46).rect(23.4,12.5).loft().val()
    lid=lid.cut(bevel)
    # Joystick stem sweep; cap stays above lid even during an assumed 0.3 press.
    jx,jy=60.325,-14.732
    lid=lid.cut(cyl(jx,jy,math.sqrt(2)*1.25+P['joystick_travel_xy_assumed']+.4,zs-1,zt+1))
    cap=cyl(jx,jy,5.2,P['joystick_cap_bottom'],P['joystick_cap_top'])
    cap=cq.Workplane(obj=cap).faces('>Z').edges().fillet(.6).val()
    socket_size=P['joystick_socket_size']
    if P['joystick_socket_shape']=='round':
        cap_hole=cyl(jx,jy,socket_size/2,8.7,10.85)
    elif P['joystick_socket_shape']=='square':
        cap_hole=box(jx-socket_size/2,jx+socket_size/2,jy-socket_size/2,jy+socket_size/2,8.7,10.85)
    else: raise ValueError('joystick_socket_shape must be round or square')
    cap=cap.cut(cap_hole)
    lead=cq.Workplane('XY',origin=(jx,jy,8.79)).rect(socket_size+.5,socket_size+.5).workplane(offset=.3).rect(socket_size,socket_size).loft().val()
    cap=cap.cut(lead)
    # Captive back plunger, installed from beneath the lid before assembly.
    bx,by=49.149,-15.875
    rest=5.4+P['back_button_rest_gap']
    plunger=cyl(bx,by,1.3,rest,6.2).fuse(cyl(bx,by,2.5,6.2,6.9),cyl(bx,by,1.7,6.9,8.8))
    # Guide cage with an internal flange stop, joined into lid roof.
    guide=cyl(bx,by,3.5,5.9,zc+.1)
    lid=lid.fuse(guide).cut(cyl(bx,by,2.85,5.8,7.1)).cut(cyl(bx,by,2.0,7.09,zt+1))
    # Separate base stop allows flange to be inserted into the lid from below.
    arm=box(bx-1.0,bx+1.0,-22.9,by,5.15,5.65)
    arm=arm.fuse(cyl(bx,by,3.25,5.15,5.65),box(bx-1,bx+1,-22.9,-21.7,zs-.05,5.65))
    arm=arm.cut(cyl(bx,by,1.65,5.0,5.8))
    base=base.fuse(arm)
    lid=lid.cut(box(bx-1.25,bx+1.25,-23.1,-21.45,zs-.1,5.9))
    # Boot/reset service pinholes, no large underside controls in this prototype.
    for x,y in [(6.731,-3.302),(6.858,-17.526)]:
        base=base.cut(cyl(x,y,1.1,z0-1,-2.3))
    # Covered SD card and underside terminations still need internal space.
    base=base.cut(box(51.5,63.3,-14.2,1.6,-2.05,-.25))
    for name,shape,_,_ in keeps:
        if name.startswith('solder_') or name.startswith('locating_peg_'):
            base=base.cut(shape)
    # Nose-skirt lead-in permits the lid to pivot down after engaging tongues.
    lead=cq.Workplane('XZ',origin=(0,10,0)).polyline([(-3,4.3),(.1,4.3),(-3,5.3)]).close().extrude(40).val()
    lid=lid.cut(lead)
    # Relief behind the upper collar pocket for the same shallow closing motion.
    for angle in [5,10,15]:
        lid=lid.cut(collar_room.rotate((-.5,0,3.2),(-.5,1,3.2),angle))
    parts={'base':base.clean(),'lid':lid.clean(),'usb_c_collar':collar.clean(),'joystick_cap':cap.clean(),'back_plunger':plunger.clean()}
    return parts,refs,keeps


def on_bed(shape,rotation=None):
    if rotation: shape=shape.rotate((0,0,0),rotation[0],rotation[1])
    b=shape.BoundingBox()
    return shape.translate((-b.xmin,-b.ymin,-b.zmin))


def export_and_check(parts,refs,keeps):
    colours={'base':(.19,.23,.29),'lid':(.42,.49,.58),'usb_c_collar':(.92,.57,.17),'joystick_cap':(.2,.24,.29),'back_plunger':(.9,.55,.15)}
    assembled=cq.Assembly(name='Fuse_Vault_V2_fit_prototype_A')
    checks={}; collisions=[]
    for name,shape in parts.items():
        assert shape.isValid() and len(shape.Solids())==1,(name,'invalid or disconnected')
        cq.exporters.export(shape,str(OUT/(name+'.step')))
        rotation=((0,1,0),90) if name=='usb_c_collar' else (((1,0,0),180) if name in ('lid','joystick_cap','back_plunger') else None)
        printable=on_bed(shape,rotation)
        cq.exporters.export(printable,str(OUT/(name+'.stl')),tolerance=.025,angularTolerance=.12)
        loaded=cq.importers.importStep(str(OUT/(name+'.step'))).val()
        assert loaded.isValid() and len(loaded.Solids())==1
        assert abs(loaded.Volume()-shape.Volume())<1e-5
        b=shape.BoundingBox()
        checks[name]={'valid':True,'solids':1,'volume_mm3':shape.Volume(),'bounds_mm':[b.xlen,b.ylen,b.zlen],'step_round_trip':True}
        import vtk
        mesh=vtk.vtkSTLReader();mesh.SetFileName(str(OUT/(name+'.stl')));mesh.MergingOn();mesh.Update()
        edges=vtk.vtkFeatureEdges();edges.SetInputConnection(mesh.GetOutputPort())
        edges.BoundaryEdgesOn();edges.NonManifoldEdgesOn();edges.FeatureEdgesOff();edges.ManifoldEdgesOff();edges.Update()
        assert edges.GetOutput().GetNumberOfCells()==0,(name,'STL has open/non-manifold edges')
        assert abs(mesh.GetOutput().GetBounds()[4])<1e-5,(name,'STL not on bed')
        checks[name].update(stl_boundary_or_nonmanifold_edges=0,stl_on_bed=True)
        assembled.add(shape,name=name,color=cq.Color(*colours[name]))
    for (a,s),(b,t) in itertools.combinations(parts.items(),2):
        volume=s.intersect(t).Volume()
        if volume>1e-5: collisions.append({'a':a,'b':b,'volume_mm3':volume})
    for name,shape in parts.items():
        for refname,other,_,_ in refs:
            volume=shape.intersect(other).Volume()
            if volume>1e-5: collisions.append({'a':name,'b':refname,'volume_mm3':volume})
    for name,shape in parts.items():
        for kn,other,_,_ in keeps:
            if not (kn.startswith('solder_') or kn.startswith('locating_peg_') or kn.endswith('_terminal_envelope') or kn=='FPC_route_provisional'): continue
            v=shape.intersect(other).Volume()
            if v>1e-5: collisions.append({'a':name,'b':kn,'volume_mm3':v})
    # Check the plunger at its lower stop; switch actuator contact is expected only
    # after travel, so do not infer real electrical actuation from this dummy solid.
    moved=parts['back_plunger'].translate((0,0,-P['back_button_max_plunger_travel']))
    plunger_case_collision=moved.intersect(parts['lid']).Volume()
    plunger_base_collision=moved.intersect(parts['base']).Volume()
    closure=[]
    reference=cq.Compound.makeCompound([s for _,s,_,_ in refs])
    for angle in [0,2,5,10,15]:
        tilted=parts['lid'].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
        closure.append({'angle_degrees':angle,'base_intersection_mm3':tilted.intersect(parts['base']).Volume(),'reference_intersection_mm3':tilted.intersect(reference).Volume(),'collar_intersection_mm3':tilted.intersect(parts['usb_c_collar']).Volume()})
    assembled.export(str(OUT/'case_assembly.step'))
    for n,s,c,side in refs: assembled.add(s,name='REF_'+n,color=cq.Color(*c))
    assembled.export(str(OUT/'case_with_board.step'))
    report={'revision':P['revision'],'parts':checks,'collisions':collisions,'plunger_case_collision_at_stop_mm3':plunger_case_collision,'plunger_base_collision_at_stop_mm3':plunger_base_collision,'sampled_lid_closure':closure,
            'nominal_usb_c_exposed_mm':P['collar_front_x']-(-11.757),
            'nominal_usb_a_exposed_mm':88.75-max(x for x,y in P['outline_xy']),
            'limitations':['No physical fit test.','USB-A seating, SD installed position and folded flex remain assumptions; square shaft nominal is drawing-derived.','No structural/load simulation. Lid closure sampled, not a continuous motion proof.','STLs are bed-oriented; STEP parts use assembly coordinates.']}
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'parameters_used.json').write_text(json.dumps(P,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    return report


if __name__=='__main__':
    parts,refs,keeps=build()
    report=export_and_check(parts,refs,keeps)
    if report['collisions'] or max(report['plunger_case_collision_at_stop_mm3'],report['plunger_base_collision_at_stop_mm3'])>1e-5 or any(max(c['base_intersection_mm3'],c['reference_intersection_mm3'],c['collar_intersection_mm3'])>1e-5 for c in report['sampled_lid_closure']):
        raise SystemExit('Prototype has collisions: inspect validation.json before printing.')
