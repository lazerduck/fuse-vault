"""Prototype D: slimmer, located PCB, bare joystick, side-release snap and detent caps.
CadQuery 2.8. Earlier prototypes are read-only baselines.
"""
from pathlib import Path
import hashlib,itertools,json,math
import cadquery as cq
import vtk
import build_case as a
ROOT=Path(__file__).resolve().parent;OUT=ROOT/'prototype-d';OUT.mkdir(exist_ok=True);a.OUT=OUT
box,cyl=a.box,a.cyl
P={'base_bottom':-4.3,'floor_top':-2.9,'lid_top':7.8,'lid_ceiling':6.6,'seam':4.4,
   'outline':[[-2.5,-23.7],[74.8,-23.7],[74.8,2.5],[66,2.5],[64,3.9],[50.5,3.9],[48.5,2.5],[-2.5,2.5]],
   'pcb_general_xy_gap':.35,'pcb_locator_side_gap':.15,'pcb_locator_end_gap':.2,
   'fpc_depth_allowance':3.0,'sd_room':[50.7,64.1,-14.8,2.5,-2.7,12],
   'latch_beam_thickness':.9,'latch_nominal_deflection':.45,'cap_detent_deflection':.4,
   'latch_breakaway_web':[71.35,71.75,-23.2,-22.8,-.55,.3],
   'cap_clearance':.15,'cap_wall':1.3,'back_dimple_depth':.25}
COL={'base':(.25,.31,.38),'lid':(.55,.64,.73),'usb_c_collar':(.94,.59,.17),'usb_c_end_cap':(.23,.38,.43),'usb_a_end_cap':(.23,.38,.43)}

def baseline_hashes():
    paths=[]
    for rev in ('a','b','c'):
        paths+=list((ROOT/('prototype-'+rev)).rglob('*'))+[ROOT/('fuse-vault-v2-fit-prototype-'+rev+'.zip')]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}

def rounded_x(x0,x1,y,z,w,h,r):
    return cq.Workplane('YZ',origin=(x0,y,z)).rect(w,h).extrude(x1-x0).edges('|X').fillet(r).val()

def blank(z0,z1,end=None):
    wp=cq.Workplane('XY').polyline(P['outline']).close().extrude(z1-z0).edges('|Z').fillet(2)
    if end:wp=wp.faces(end).edges().chamfer(.35)
    return wp.translate((0,0,z0)).val()

def latch_side(shape):
    return shape.mirror('XZ',(0,-10.6,0))

def cap(which):
    c=which=='c';lo,hi=(-14.557,5.2) if c else (69.5,91.55)
    # Same symmetric section at both ends after removing the screw shoulder.
    outer=rounded_x(lo,hi,-10.6,1.75,29.1,15,1.5)
    outer=cq.Workplane(obj=outer).faces('<X' if c else '>X').edges().fillet(.5).val()
    inner=rounded_x(lo+1.3 if c else lo-1,hi+1 if c else hi-1.3,-10.6,1.75,26.5,12.4,.3)
    hard=outer.cut(inner)
    seat=-2.65 if c else 74.95;start=seat-1.6 if c else seat+1.6
    for wall_z,tip_z in ((8.,6.65),(-4.55,-3.3)):
        ramp=cq.Workplane('XZ',origin=(0,-3,0)).polyline([(start,wall_z),(seat,tip_z),(seat,wall_z)]).close().extrude(16).val()
        hard=hard.fuse(ramp)
    r0,r1=(1,3) if c else (70.2,72.2)
    ribs=[]
    for y in (-23.95,2.75):
        rib=cq.Workplane('YZ',origin=(r0+.65,y,-1)).circle(.65).extrude(r1-r0-1.3).val()
        for x in (r0+.65,r1-.65):rib=rib.fuse(cq.Workplane('XY').sphere(.65).val().translate((x,y,-1)))
        ribs.append(rib.clean())
    return hard.fuse(*ribs).clean(),hard.clean()

def build():
    a.ref.P['access']['fpc_bend_depth']=P['fpc_depth_allowance']
    refs,keeps=a.corrected_reference()
    base=blank(-4.3,4.4,'<Z');lid=blank(4.4,7.8,'>Z')
    cavity=box(-.6,73.4,-21.85,.7,-2.9,6.6)
    base=base.cut(cavity);lid=lid.cut(cavity)
    base=base.fuse(box(-2.5,.7,-18.3,-3,-2.9,4.4))
    lid=lid.fuse(box(-2.5,.7,-18.3,-3,4.4,6.65))
    # Open board loading envelope; short locators are added below its entry.
    base=base.cut(box(-.477,73.558,-21.587,.35,0,15))
    # Preserve existing collar, including the successful bore and flange.
    collar=cq.importers.importStep(str(ROOT/'prototype-c/usb_c_collar.step')).val()
    room=a.capsule_x(-5,-1.65,11.95,6.1).fuse(a.capsule_x(-1.65,-.35,14.7,7.9))
    connector=a.capsule_x(-15,2.3,9.35,3.4).fuse(box(-.8,2.4,-15.343,-5.993,.15,3.55))
    for cut in (room,connector,box(69.4,95,-17.8,-4.3,1.05,6.75)):
        base=base.cut(cut);lid=lid.cut(cut)
    base=base.cut(box(-5,-1.65,-16.643,-4.693,1.85,15)).cut(box(-1.65,-.35,-18.018,-3.318,1.85,15))
    # Original support locations and board seating height, with local XY stops.
    for x,y in a.P['support_xy']:
        upper=y>-10;ya,yb=(-.55,1.15) if upper else (-22.2,-20.7)
        base=base.fuse(box(x-1,x+1,ya,yb,-2.9,0))
        lid=lid.fuse(box(x-1,x+1,ya,yb,1.8,6.65))
        base=base.cut(box(x-1.25,x+1.25,ya-.2,yb+.2,1.6,4.5))
        profile=[(.15,0),(1.15,0),(1.15,2.3),(.45,2.3),(.15,1.6)] if upper else [(-21.387,0),(-22.2,0),(-22.2,2.3),(-21.687,2.3),(-21.387,1.6)]
        base=base.fuse(cq.Workplane('YZ',origin=(x-1,0,0)).polyline(profile).close().extrude(2).val())
        # Relief above a locator lets the lid's paired support close over it.
        lid=lid.cut(box(x-1.2,x+1.2,.12 if upper else -22.3,1.3 if upper else -21.357,1.75,2.55))
    for y in (-2,-19.8):
        for profile in ([(-1.3,0),(-.327,0),(-.327,1.6),(-.627,2.3),(-1.3,2.3)],[(73.408,0),(74.2,0),(74.2,2.3),(73.708,2.3),(73.408,1.6)]):
            base=base.fuse(cq.Workplane('XZ',origin=(0,y+.6,0)).polyline(profile).close().extrude(1.2).val())
    for y in (-.9,-20.8):
        base=base.cut(box(-1.9,2,y-1.5,y+1.5,2.05,3.6)).cut(box(-.35,1.95,y-1.4,y+1.4,3.4,4.5))
        lid=lid.fuse(box(.1,1.7,y-1.15,y+1.15,2.45,6.65),box(-1.55,1.7,y-1.15,y+1.15,2.45,3.05))
    for ya,yb in ((.8,1.45),(-22.6,-21.95)):
        base=base.fuse(box(29,39,ya,yb,4.35,5.2));lid=lid.cut(box(28.7,39.3,ya-.25,yb+.25,4.3,5.45))
    base=base.cut(box(*P['sd_room']))
    for name,shape,_,_ in keeps:
        if name.startswith(('solder_','locating_peg_')) or name=='FPC_route_provisional':base=base.cut(shape)
    for x,y in ((6.731,-3.302),(6.858,-17.526)):base=base.cut(cyl(x,y,1.1,-5,-2.3))
    # Screen aperture follows previous geometry with its bevel at the new roof.
    ctr=(32.45146,-11.74208)
    lid=lid.cut(box(ctr[0]-11.25,ctr[0]+11.25,ctr[1]-5.8,ctr[1]+5.8,6.5,9))
    lid=lid.cut(cq.Workplane('XY',origin=(*ctr,7.35)).rect(22.5,11.6).workplane(offset=.46).rect(23.4,12.5).loft().val())
    lid=lid.cut(cyl(60.325,-14.732,math.sqrt(2)*1.25+1+.4,3.4,9))
    # Preserve 0.8 mm leaf, slot footprint, switch rest gap and 0.55 mm stop gap.
    for x in (46.824,51.474):lid=lid.cut(cq.Workplane('XY',origin=(x,-12.1375,6.1)).slot2D(13.325,.65,90).extrude(2).val())
    lid=lid.cut(box(46.824,51.474,-18.8,-18.15,6.1,8))
    lid=lid.cut(box(47.149,51.149,-18.15,-7.3,6.5,7))
    lid=lid.cut(cq.Workplane('YZ',origin=(47.149,0,0)).polyline([(-7.3,6.5),(-5.8,6.5),(-5.8,6.6),(-7.3,7)]).close().extrude(4).val())
    lid=lid.fuse(cyl(49.149,-15.875,1.2,5.65,7.05))
    for x0,x1,stem0,stem1 in ((45.5,47.649,45.5,46.7),(50.649,52.8,51.6,52.8)):
        lid=lid.fuse(box(x0,x1,-17.3,-14.5,5.65,6.45),box(stem0,stem1,-17.3,-14.5,5.65,6.7))
    lid=lid.cut(cyl(49.149,-15.875,1.35,7.8-P['back_dimple_depth'],8))
    # Nose lead-in and collar relief preserve the successful hooked closure.
    lid=lid.cut(cq.Workplane('XZ',origin=(0,10,0)).polyline([(-3,4.3),(.1,4.3),(-3,5.3)]).close().extrude(40).val())
    for angle in (5,10,15):lid=lid.cut(room.rotate((-.5,0,3.2),(-.5,1,3.2),angle))
    # Long horizontal flexure in the lower-Y side wall at the USB-A end.
    base=base.cut(latch_side(box(62,72.7,.6,2.7,-.45,.2)))
    base=base.cut(latch_side(box(72,72.7,.6,2.7,.19,4.5)))
    base=base.cut(latch_side(box(63,72,.6,1.6,.2,4.5)))
    # Round the slit root to avoid a sharp internal notch.
    base=base.cut(latch_side(cq.Workplane('XZ',origin=(62,2.7,-.125)).circle(.325).extrude(2.1).val()))
    base=base.cut(latch_side(box(68.6,71.4,1.5,2.7,1.4,3.55)))
    tongue=box(69,71,.75,1.5,2,6.65)
    tooth=cq.Workplane('YZ',origin=(69,0,0)).polyline([(1.45,2),(2.05,2.8),(2.05,3.3),(1.45,3.3)]).close().extrude(2).val()
    lid=lid.fuse(latch_side(tongue),latch_side(tooth))
    # Recesses hold the cap's TPU ribs once fully seated, rather than friction only.
    for x0,x1 in ((.7,3.3),(69.9,72.5)):
        for y0,y1 in ((2,2.7),(-23.9,-23.2)):base=base.cut(box(x0,x1,y0,y1,-1.8,-.2))
    # One-line-wide sacrificial prop supports the free end of the printed bridge.
    # Clip its exposed middle out of the lower slot before flexing the latch.
    base=base.fuse(box(*P['latch_breakaway_web']))
    parts={'base':base.clean(),'lid':lid.clean(),'usb_c_collar':collar}
    hard={}
    for end in ('c','a'):parts['usb_'+end+'_end_cap'],hard[end]=cap(end)
    coupons={n+'_latch_coupon':parts[n].intersect(box(60,75,-24,-17.7,-4.4,8)).clean() for n in ('base','lid')}
    return parts,refs,keeps,hard,coupons

def validate_export(parts,refs,keeps,hard,coupons):
    fail=[];checks=[];exported={};allowed=[]
    def check(label,s,t):
        v=s.intersect(t).Volume();checks.append({'check':label,'volume_mm3':v})
        if v>1e-5:fail.append(checks[-1])
    for n,s in {**parts,**coupons}.items():
        assert s.isValid() and len(s.Solids())==1,(n,'invalid/disconnected')
        cq.exporters.export(s,str(OUT/(n+'.step')))
        rot=((1,0,0),180) if n.startswith('lid') else (((0,1,0),-90) if n=='usb_c_end_cap' else (((0,1,0),90) if n in ('usb_c_collar','usb_a_end_cap') else None))
        cq.exporters.export(a.on_bed(s,rot),str(OUT/(n+'.stl')),tolerance=.025,angularTolerance=.12)
        loaded=cq.importers.importStep(str(OUT/(n+'.step'))).val()
        assert loaded.isValid() and len(loaded.Solids())==1 and abs(loaded.Volume()-s.Volume())<1e-5
        mesh=vtk.vtkSTLReader();mesh.SetFileName(str(OUT/(n+'.stl')));mesh.MergingOn();mesh.Update()
        edges=vtk.vtkFeatureEdges();edges.SetInputConnection(mesh.GetOutputPort());edges.BoundaryEdgesOn();edges.NonManifoldEdgesOn();edges.FeatureEdgesOff();edges.ManifoldEdgesOff();edges.Update()
        assert edges.GetOutput().GetNumberOfCells()==0 and abs(mesh.GetOutput().GetBounds()[4])<1e-5,n
        bb=s.BoundingBox();exported[n]={'valid_single_solid':True,'STEP_roundtrip':True,'STL_closed_manifold_on_bed':True,'dimensions_mm':[bb.xlen,bb.ylen,bb.zlen]}
    for (n,s),(m,t) in itertools.combinations(parts.items(),2):check(n+' vs '+m,s,t)
    protected=[(n,s) for n,s,_,_ in keeps if n.startswith(('solder_','locating_peg_')) or n.endswith('_terminal_envelope') or n=='FPC_route_provisional']
    refsolid=cq.Compound.makeCompound([s for _,s,_,_ in refs]+[s for _,s in protected])
    for n,s in parts.items():
        check(n+' vs electronics and reserves',s,refsolid)
    carried=cq.Compound.makeCompound([refsolid,parts['usb_c_collar']])
    for z in (0,.25,.5,1,2,3,4,6,8,12):check('board insertion Z='+str(z),carried.translate((0,0,z)),parts['base'])
    check('continuous PCB insertion',box(-.127,73.208,-21.237,0,0,13.6),parts['base'])
    check('continuous SD loading reserve',box(50.7,64.1,-14.8,2.5,-2.7,11.75),parts['base'])
    for n,x0,x1,w,h in (('body',-4.5,-.6,11.45,5.6),('flange',-1.4,-.6,14.2,7.4)):
        check('continuous collar '+n,rounded_x(x0,x1,-10.668,7.85,w,h+12,h/2),parts['base'])
    beamzone=latch_side(box(61.5,72.1,1.59,2.6,.19,4.41))
    released_base=parts['base'].cut(box(71.3,71.8,-23.21,-22.79,-.45,.2))
    fixed=released_base.cut(beamzone)
    for angle in (0,.25,.5,.75,1,1.5,2,5,10,15):
        tilted=parts['lid'].rotate((-.5,0,3.2),(-.5,1,3.2),-angle)
        check('closure fixed base '+str(angle),tilted,fixed)
        check('closure electronics '+str(angle),tilted,refsolid)
        check('closure collar '+str(angle),tilted,parts['usb_c_collar'])
        allowed.append({'closure_angle':angle,'latch_elastic_interference_mm3':tilted.intersect(released_base.intersect(beamzone)).Volume()})
    receiver=released_base.intersect(latch_side(box(68,72,1.59,2.6,.19,4.41))).translate((0,-.5,0))
    check('receiver release clearance at 0.5mm outward',receiver,parts['lid'])
    # Exact bare shaft movement proxy, not a simulation of switch internals.
    shaft=box(59.075,61.575,-15.982,-13.482,6.6,10.6)
    for angle in range(0,360,45):
        r=math.radians(angle);check('bare shaft XY sweep '+str(angle),shaft.translate((math.cos(r),math.sin(r),-.25)),parts['lid'])
    for end,s in hard.items():
        for dx in (.5,1,2,4,8,15,25):
            shift=(dx*(-1 if end=='c' else 1),0,0)
            check('cap rigid clearance '+end+str(dx),s.translate(shift),cq.Compound.makeCompound([parts['base'],parts['lid'],parts['usb_c_collar'],refsolid]))
            full=parts['usb_'+end+'_end_cap'].translate(shift)
            check('cap ribs avoid electronics '+end+str(dx),full,cq.Compound.makeCompound([parts['lid'],parts['usb_c_collar'],refsolid]))
            allowed.append({'cap':end,'removal_mm':dx,'TPU_detent_interference_mm3':full.intersect(parts['base']).Volume()})
    assy=cq.Assembly(name='V2_prototype_D')
    for n,s in parts.items():assy.add(s,name=n,color=cq.Color(*COL[n]))
    assy.export(str(OUT/'case_assembly.step'))
    for n,s,c,side in refs:assy.add(s,name='REF_'+n,color=cq.Color(*c))
    assy.export(str(OUT/'case_with_board.step'))
    report={'revision':'D','parts':exported,'checks':checks,'failures':fail,'intentional_elastic_motion':allowed,
            'body_mm':[77.3,27.6,12.1],'bare_joystick_overall_height_mm':14.9,
            'limitations':['Clip the sacrificial support web from the lower latch slot before testing. Latch force/fatigue and TPU cap retention require physical tests.','Latch closing samples permit interference only in the designed flexure. No stress simulation.','FPC depth allowance reduced from 3.5 to 3.0 mm on user feedback; never clamp or crease the real flex.','Reference heights/card position remain partly assumed; tolerances and real fit unverified.','Back leaf thickness, slot footprint and actuator gap preserved; thinner roof/root and tactile recess require recheck.']}
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'parameters_used.json').write_text(json.dumps(P,indent=2)+'\n')
    print('Failures:',json.dumps(fail,indent=2),flush=True)
    assert not fail,'Inspect validation.json'

if __name__=='__main__':
    before=baseline_hashes()
    parts,refs,keeps,hard,coupons=build()
    validate_export(parts,refs,keeps,hard,coupons)
    assert baseline_hashes()==before,'Earlier prototype changed'
    (OUT/'baseline_preservation.json').write_text(json.dumps(before,indent=2)+'\n')
    print('D passed, previous outputs unchanged.',flush=True)
