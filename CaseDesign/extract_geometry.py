"""Extract this EasyEDA export; fail on unsupported outline/drill syntax.
Coordinates and dimensions are millimetres, unchanged from the original files.
"""
from pathlib import Path
import csv, json, re, math, hashlib, io
ROOT=Path(__file__).resolve().parent
SRC=ROOT/'source'

def outline():
    text=(SRC/'Gerber_BoardOutlineLayer.GKO').read_text()
    assert '%FSLAX45Y45*%' in text and '%MOMM*%' in text
    loops=[]; pts=[]
    for line in text.splitlines():
        if not line.startswith('G01X'): continue
        m=re.fullmatch(r'G01X(-?\d+)Y(-?\d+)D0([12])\*',line)
        if not m: raise ValueError(line)
        x,y,op=m.groups(); p=[int(x)/1e5,int(y)/1e5]
        if op=='2':
            if pts: loops.append(pts)
            pts=[p]
        else: pts.append(p)
    if pts:loops.append(pts)
    for p in loops:
        assert len(p)==5 and p[0]==p[-1]
        assert all(a[0]==b[0] or a[1]==b[1] for a,b in zip(p,p[1:]))
    return sorted(loops,key=lambda p:abs((max(x for x,y in p)-min(x for x,y in p))*(max(y for x,y in p)-min(y for x,y in p))),reverse=True)

def drills():
    result={}
    for path in sorted(SRC.glob('Drill*.DRL')):
        text=path.read_text(); assert 'METRIC,LZ,0000.00000' in text
        sizes={};tool=None
        for n,line in enumerate(text.splitlines(),1):
            m=re.fullmatch(r'T(\d+)C([\d.]+)',line)
            if m:sizes[int(m[1])]=float(m[2]);continue
            m=re.fullmatch(r'T(\d+)',line)
            if m:tool=int(m[1]);continue
            if not line.startswith('X'):continue
            m=re.fullmatch(r'X(-?[\d.]+)Y(-?[\d.]+)(?:G85X(-?[\d.]+)Y(-?[\d.]+))?',line)
            if not m:raise ValueError((path.name,n,line))
            x,y=map(float,m.groups()[:2]); x2,y2=(map(float,m.groups()[2:]) if m[3] else (x,y))
            d=sizes[tool]; key=(x,y,x2,y2,d)
            if key in result:result[key]['sources']+=';'+path.name+':'+str(n);continue
            result[key]=dict(x=x,y=y,end_x=x2,end_y=y2,diameter=d,kind='slot' if m[3] else 'round',plating='NPTH' if 'NPTH' in path.name else 'PTH',sources=path.name+':'+str(n))
    return list(result.values())

def placements():
    def table(name):
        raw=(SRC/name).read_bytes()
        return csv.DictReader(io.StringIO(raw.decode('utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig')),delimiter='\t')
    bom={}
    for row in table('assembly-a.csv'):
        for ref in row['Designator'].split(','):bom[ref]=row
    rows=[]
    for row in table('assembly-b.csv'):
        ref=row['Designator']; b=bom.get(ref,{})
        rows.append(dict(ref=ref,x=float(row['Mid X'].replace('mm','')),y=float(row['Mid Y'].replace('mm','')),rotation=float(row['Rotation']),side=row['Layer'],footprint=row['Footprint'],device=row['Device'],manufacturer=b.get('Manufacturer',''),supplier_part=b.get('Supplier Part','')))
    assert len({r['ref'] for r in rows})==len(rows)
    return rows

def main():
    loops=outline(); hs=drills(); ps=placements()
    data=dict(units='mm',coordinates='Original Gerber XY; bottom PCB Z=0; top PCB Z=thickness',outline=loops[0],cutouts=loops[1:],drills=hs,placements=ps)
    (ROOT/'geometry.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    for filename,rows in [('drill_schedule.csv',hs),('placements.csv',ps)]:
        with (ROOT/filename).open('w',newline='',encoding='utf-8-sig') as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in SRC.iterdir() if p.is_file()}
    (ROOT/'source_hashes.json').write_text(json.dumps(hashes,indent=2))
    print(f'Extracted {len(hs)} unique drills/slots and {len(ps)} placements.')
    return data
if __name__=='__main__':main()
