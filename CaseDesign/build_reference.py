"""Parametric enclosure reference, not a vendor-detail PCBA replica.
Run: python extract_geometry.py && python build_reference.py
All settings are in parameters.json; CPL placement centres are never mirrored.
"""
from pathlib import Path
import json, math, csv
import cadquery as cq
ROOT=Path(__file__).resolve().parent
G=json.loads((ROOT/'geometry.json').read_text())
P=json.loads((ROOT/'parameters.json').read_text())
T=P['pcb_thickness']
parts=[]; keeps=[]; rows=[]

def box(x,y,z,sx,sy,sz):
    return cq.Workplane('XY').box(sx,sy,sz,centered=(True,True,False)).translate((x,y,z)).val()

def add(name,shape,color,side='both',evidence='',collection=None):
    target=parts if collection is None else collection
    assert shape.isValid(),name
    target.append((name,shape,color,side))
    if target is parts:
        b=shape.BoundingBox()
        rows.append(dict(name=name,side=side,xmin=b.xmin,xmax=b.xmax,ymin=b.ymin,ymax=b.ymax,zmin=b.zmin,zmax=b.zmax,evidence=evidence))

def build():
    parts.clear();keeps.clear();rows.clear()
    board=cq.Workplane('XY').polyline(G['outline'][:-1]).close().extrude(T)
    for loop in G['cutouts']:
        board=board.cut(cq.Workplane('XY').polyline(loop[:-1]).close().extrude(T))
    cutters=[]
    for h in G['drills']:
        if h['kind']=='round':
            cutter=cq.Workplane('XY').center(h['x'],h['y']).circle(h['diameter']/2).extrude(T)
        else:
            dx=h['end_x']-h['x'];dy=h['end_y']-h['y']
            cutter=cq.Workplane('XY').center((h['x']+h['end_x'])/2,(h['y']+h['end_y'])/2).slot2D(math.hypot(dx,dy)+h['diameter'],h['diameter'],math.degrees(math.atan2(dy,dx))).extrude(T)
        cutters.append(cutter.val())
    board=board.val().cut(*cutters)
    add('PCB',board,(0.10,0.38,0.19),evidence='Exact original GKO and unique Excellon drill/slot geometry; nominal thickness')
    cq.exporters.export(board,str(ROOT/'pcb.step'))
    cq.exporters.export(board,str(ROOT/'pcb.stl'),tolerance=0.01,angularTolerance=0.1)
    for r in G['placements']:
        ref=r['ref'];side=r['side'];x=r['x'];y=r['y'];fp=r['footprint']
        if fp.startswith('Test-Point'):continue
        if ref in P['special_components']:
            s=P['special_components'][ref];sx,sy,h=s['size'];x+=s['offset_xy'][0];y+=s['offset_xy'][1]
            z=T+s['bottom_from_surface'] if side=='T' else -s['bottom_from_surface']-h
            if ref=='SW3':
                sh=box(x,y,z,sx,sy,s['body_height']).fuse(cq.Workplane('XY').center(x,y).circle(s['stem_diameter']/2).extrude(h-s['body_height']).translate((0,0,z+s['body_height'])).val())
            elif ref.startswith('SW'):
                # Preserve maximum height, approximate actuator and housing.
                base=h*0.70
                if side=='T':
                    sh=box(x,y,z,sx,sy,base).fuse(cq.Workplane('XY').center(x,y).circle(1.15).extrude(h-base).translate((0,0,z+base)).val())
                else:
                    sh=box(x,y,z+h-base,sx,sy,base).fuse(cq.Workplane('XY').center(x,y).circle(1.15).extrude(h-base).translate((0,0,z)).val())
            else:sh=box(x,y,z,sx,sy,h)
            color=(0.72,0.75,0.8) if ref in ('J1','J2','CARD1','SW1','SW2','SW3','SW4') else (0.21,0.23,0.27)
            add(ref,sh,color,side,s['evidence'])
        else:
            import re
            dims={'0402':(1,.5),'0603':(1.6,.8),'0805':(2,1.25)}
            key=next((k for k in dims if fp.endswith(k)),None)
            if key:sx,sy=dims[key]
            else:
                m=re.search(r'[_-]L([\d.]+)-W([\d.]+)',fp)
                if not m:raise ValueError('Unmodelled package: '+fp)
                sx,sy=map(float,m.groups())
                lead=re.search(r'-LS([\d.]+)',fp)
                if lead:sy=max(sy,float(lead[1]))
                key=next((k for k in P['package_heights_assumed'] if fp.startswith(k)),'other')
            h=P['package_heights_assumed'][key]
            z=T if side=='T' else -h
            sh=box(0,0,z,sx,sy,h).rotate((0,0,0),(0,0,1),r['rotation']).translate((x,y,0))
            add(ref,sh,(0.28,0.29,0.32),side,'CPL centre/rotation; footprint package envelope including lead span where named; height assumption')
    d=P['display']
    if d['enabled']:
        add('TFT_PLACEHOLDER',box((d['xmin']+d['xmax'])/2,(d['ymin']+d['ymax'])/2,T+d['gap_above_pcb'],d['xmax']-d['xmin'],d['ymax']-d['ymin'],d['thickness']),(0.1,0.65,0.82),'T',d['evidence'])
    cx=P['clearance_xy'];cz=P['clearance_z']
    for name,sh,color,side in parts:
        b=sh.BoundingBox()
        add(name+'_clearance',box((b.xmin+b.xmax)/2,(b.ymin+b.ymax)/2,b.zmin-cz,b.xlen+2*cx,b.ylen+2*cx,b.zlen+2*cz),(0.95,0.55,0.15),side,collection=keeps)
    by={r['name']:r for r in rows}; a=P['access']
    for ref,direction in [('J1',1),('J2',-1)]:
        r=by[ref];start=r['xmax'] if direction==1 else r['xmin'];length=a['usb_mating_length']
        add(ref+'_mating_access',box(start+direction*length/2,(r['ymin']+r['ymax'])/2,r['zmin']-a['usb_mating_extra_z'],length,r['ymax']-r['ymin']+2*a['usb_mating_extra_xy'],r['zmax']-r['zmin']+2*a['usb_mating_extra_z']),(0.95,.6,.1),collection=keeps)
    r=by['CARD1']
    add('microSD_withdrawal_access',box((r['xmin']+r['xmax'])/2,r['ymax']+a['sd_withdrawal_length']/2,-a['sd_slot_height'],a['sd_slot_width'],a['sd_withdrawal_length'],a['sd_slot_height']),(.9,.6,.2),'B',collection=keeps)
    for ref in ['SW1','SW2','SW3','SW4']:
        r=by[ref];x=(r['xmin']+r['xmax'])/2;y=(r['ymin']+r['ymax'])/2
        dia=a['joystick_motion_diameter'] if ref=='SW3' else a['button_access_diameter']
        h=a['joystick_motion_extra_height'] if ref=='SW3' else a['button_access_length']
        z=r['zmin']-h if r['side']=='B' else r['zmax']
        if ref=='SW3':z=T+P['special_components']['SW3']['body_height'];h=r['zmax']-z+a['joystick_motion_extra_height']
        sh=cq.Workplane('XY').center(x,y).circle(dia/2).extrude(h).translate((0,0,z)).val()
        add(ref+'_actuation_access',sh,(.9,.6,.2),r['side'],collection=keeps)
    # Provisional flex corridor: from bottom FPC to board slot. Not a designed flex route.
    add('FPC_route_provisional',box(17.4,-11.65,-a['fpc_bend_depth'],12,10,a['fpc_bend_depth']),(.9,.6,.2),'B',collection=keeps)
    for h in G['drills']:
        if h['diameter']>=1 and h['plating']=='PTH':
            sh=cq.Workplane('XY').center(h['x'],h['y']).circle(h['diameter']/2+.3).extrude(a['solder_projection']).translate((0,0,-a['solder_projection'])).val()
            add('solder_'+str(len(keeps)),sh,(.9,.6,.2),'B',collection=keeps)
        elif h['plating']=='NPTH':
            # Occupied locating holes are not available for enclosure fasteners.
            sh=cq.Workplane('XY').center(h['x'],h['y']).circle(h['diameter']/2+.3).extrude(T+2*P['locating_peg_projection_assumed']).translate((0,0,-P['locating_peg_projection_assumed'])).val()
            add('locating_peg_'+str(len(keeps)),sh,(.9,.6,.2),collection=keeps)
    placements={r['ref']:r for r in G['placements']}
    for ref,s in P['terminal_envelopes_assumed'].items():
        r=placements[ref];sx,sy=s['size_xy'];h=s['height'];z=T if r['side']=='T' else -h
        sh=box(r['x']+s['offset_xy'][0],r['y']+s['offset_xy'][1],z,sx,sy,h)
        add(ref+'_terminal_envelope',sh,(.9,.6,.2),r['side'],collection=keeps)
    for filename,items in [('reference',parts),('keepouts',keeps)]:
        assy=cq.Assembly(name='Fuse_Vault_'+filename)
        for name,sh,color,side in items:assy.add(sh,name=name,color=cq.Color(*color))
        assy.export(str(ROOT/(filename+'.step')))
    # STL is a multi-solid preview; not a single printable case or boolean union.
    compound=cq.Compound.makeCompound([s for _,s,_,_ in parts])
    cq.exporters.export(compound,str(ROOT/'reference.stl'),tolerance=.02,angularTolerance=.1)
    with (ROOT/'component_envelopes.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    return parts,keeps

if __name__=='__main__':
    build()
    print(f'Exported {len(parts)} reference solids and {len(keeps)} keep-out volumes.')
