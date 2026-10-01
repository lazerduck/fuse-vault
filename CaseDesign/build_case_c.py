"""Prototype C: top-loading populated board and externally accessible M2 nut.
Reuses B controls and accessories; leaves A/B files untouched.
"""
from pathlib import Path
import hashlib, json, math
import cadquery as cq
import build_case_b as b
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'prototype-c';OUT.mkdir(exist_ok=True)
b.OUT=OUT;b.a.OUT=OUT
box,cyl=b.box,b.cyl
P={'revision':'C','pcb_xy_clearance':.6,'entry_extra_clearance':.3,
   'usb_a_body_extension':1.2,'sd_room':[50.7,64.1,-14.8,2.5,-2.7,12],
   'nut_pocket_af':4.6,'nut_floor_z':6.4,'nut_nominal_af':4,'nut_nominal_height':1.6,
   'screw_clearance_diameter':2.6,'head_clearance_diameter':4.4,'head_seat_z':-2.,'screw_nominal_length':10.}

def baseline_hashes():
    paths=[]
    for rev in ('a','b'):
        paths+=list((ROOT/('prototype-'+rev)).rglob('*'))+[ROOT/('fuse-vault-v2-fit-prototype-'+rev+'.zip')]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}

def hexagon(af,z0,z1):
    return cq.Workplane('XY',origin=(68,3.4,z0)).polygon(6,af/math.cos(math.pi/6)).extrude(z1-z0).val()

def build():
    # Preserve a printable end wall after making the full PCB outline top-loadable.
    b.a.P['outline_xy']=[[x+P['usb_a_body_extension'] if x==73.6 else x,y] for x,y in b.a.P['outline_xy']]
    parts,refs,keeps,hard,coupon=b.build()
    base,lid=parts['base'],parts['lid']
    xmin,xmax=-.127-P['pcb_xy_clearance'],73.208+P['pcb_xy_clearance']
    ymin,ymax=-21.237-P['pcb_xy_clearance'],P['pcb_xy_clearance']
    # Open above the PCB underside; six bottom pads remain at Z=0.
    base=base.cut(box(xmin,xmax,ymin,ymax,0,14))
    # 0.3 mm lead-in at the top edge, clear of the support pads.
    entry=cq.Workplane('XY',origin=((xmin+xmax)/2,(ymin+ymax)/2,3.5)).rect(xmax-xmin,ymax-ymin).workplane(offset=.9).rect(xmax-xmin+.6,ymax-ymin+.6).loft().val()
    base=base.cut(entry)
    # Full upward clearance above the collar's existing rounded seats. The lid
    # captures it when closed; no bridge remains across the loading channel.
    base=base.cut(box(-5,-1.65,-16.643,-4.693,1.85,14))
    base=base.cut(box(-1.65,-.35,-18.018,-3.318,1.85,14))
    # Card fitted before board installation; room continues up to the open seam.
    base=base.cut(box(*P['sd_room']))
    # Restore screw column, replacing the inward-facing old nut pocket entirely.
    lid=lid.fuse(cyl(68,3.4,3.2,4.4,8.2))
    lid=lid.cut(cyl(68,3.4,1.3,4.3,9))
    lid=lid.cut(hexagon(P['nut_pocket_af'],P['nut_floor_z'],9))
    # Widen the entry slightly at the roof, for fingertip placement of the nut.
    lead=cq.Workplane('XY',origin=(68,3.4,7.95)).polygon(6,4.6/math.cos(math.pi/6)).workplane(offset=.3).polygon(6,5./math.cos(math.pi/6)).loft().val()
    lid=lid.cut(lead)
    base=base.cut(cyl(68,3.4,1.3,-7,5))
    base=base.cut(cyl(68,3.4,2.2,-7,P['head_seat_z']))
    parts['base']=base.clean();parts['lid']=lid.clean()
    # The same physical USB-A cover seats 1.2 mm farther out on the longer body.
    parts['usb_a_end_cap']=parts['usb_a_end_cap'].translate((1.2,0,0))
    hard['a']=hard['a'].translate((1.2,0,0))
    hardware={'M2_nut_reference':hexagon(4,6.4,8).cut(cyl(68,3.4,1.1,6.3,8.1)),
              'M2x10_screw_reference':cyl(68,3.4,1,-2,8).fuse(cyl(68,3.4,2,-4,-2))}
    return parts,refs,keeps,hard,coupon,hardware

def insertion_checks(parts,refs,keeps,hardware):
    checks=[];fail=[]
    def check(label,moving,fixed):
        vol=moving.intersect(fixed).Volume()
        checks.append({'check':label,'intersection_mm3':vol})
        if vol>1e-5:fail.append(checks[-1])
    # Translate all installed component envelopes together, including the card.
    # Solder/pin envelopes move with the board too, not just package bodies.
    carried=[s for _,s,_,_ in refs]+[parts['usb_c_collar']]
    carried += [s for n,s,_,_ in keeps if n.startswith(('solder_','locating_peg_')) or n.endswith('_terminal_envelope') or n=='FPC_route_provisional']
    moving=cq.Compound.makeCompound(carried)
    for height in (0,.25,.5,1,1.5,2,3,4,5,6,8,12):
        check('populated_board_and_collar_drop_z_'+str(height),moving.translate((0,0,height)),parts['base'])
    # Conservative continuous sweeps for the three reported obstructions.
    # The filled PCB rectangle includes holes/cutout, so this overestimates its
    # swept material. Extruded rounded rectangles are exact vertical sweeps of
    # the collar's capsule exteriors (its central bore is conservatively filled).
    check('continuous_PCB_outline_sweep_12mm',box(-.127,73.208,-21.237,0,0,13.6),parts['base'])
    for name,x0,x1,w,h in (('body',-4.5,-.6,11.45,5.6),('flange',-1.4,-.6,14.2,7.4)):
        sweep=b.rounded_x(x0,x1,-10.668,7.85,w,h+12,h/2)
        check('continuous_collar_'+name+'_sweep_12mm',sweep,parts['base'])
    check('continuous_SD_clearance_sweep_12mm',box(50.7,64.1,-14.8,2.5,-2.7,11.75),parts['base'])
    # Clearance reserve also has an unbroken path, beyond the nominal card.
    sd=box(50.7,64.1,-14.8,2.5,-2.7,-.25)
    for height in (0,1,2,3,4,6,8):check('SD_clearance_drop_z_'+str(height),sd.translate((0,0,height)),parts['base'])
    connector=next(s for n,s,_,_ in refs if n=='J2')
    for dx in (-15,-10,-5,-2,-1,0):check('collar_preinstallation_x_'+str(dx),parts['usb_c_collar'].translate((dx,0,0)),connector)
    assembled=cq.Compound.makeCompound([parts['base'],parts['lid']]+[s for _,s,_,_ in refs])
    for dz in (0,1,3,8):check('nut_from_above_z_'+str(dz),hardware['M2_nut_reference'].translate((0,0,dz)),assembled)
    for dz in (0,-2,-5,-10,-15):check('screw_from_below_z_'+str(dz),hardware['M2x10_screw_reference'].translate((0,0,dz)),assembled)
    report={'checks':checks,'failures':fail,'limitations':['Continuous conservative sweeps checked for PCB outline, collar exteriors and SD clearance. Other components and hardware use discrete samples; this is not a print-tolerance proof.','Reference card extension and package heights are provisional; actual C fit must be tested.','Board is inserted with SD card latched and collar fitted, before fitting lid or D-pad cap.']}
    (OUT/'assembly_validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Assembly failures:',json.dumps(fail),flush=True)
    assert not fail,'Insertion path obstructed: see assembly_validation.json'
    return report

if __name__=='__main__':
    before=baseline_hashes()
    parts,refs,keeps,hard,coupon,hardware=build()
    insertion_checks(parts,refs,keeps,hardware)
    b.export_check(parts,refs,keeps,hard,coupon,revision="C")
    report=json.loads((OUT/'validation.json').read_text());report['revision']='C'
    report['limitations'][0]='Actual V2 board tried in previous case; assembly obstructions reported. Prototype C physical fit pending.'
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'parameters_used.json').write_text(json.dumps({'C':P,'B_controls':b.P,'base_parameters':b.a.P},indent=2)+'\n')
    assy=cq.Assembly(name='V2_prototype_C_with_hardware')
    for n,s in parts.items():assy.add(s,name=n)
    for n,s in hardware.items():assy.add(s,name=n,color=cq.Color(.7,.72,.76))
    assy.export(str(OUT/'case_with_hardware.step'))
    assert baseline_hashes()==before,'Earlier prototype files changed'
    (OUT/'baseline_preservation.json').write_text(json.dumps(before,indent=2)+'\n')
    print('C build and validation passed; A/B unchanged.',flush=True)
