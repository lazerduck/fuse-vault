"""Independently check exported STEP/STL variants against L's working geometry."""
from pathlib import Path
import hashlib,itertools,json
import cadquery as cq
import trimesh
ROOT=Path(__file__).resolve().parent
BASE=ROOT/'prototype-l'
def box(x0,x1,y0,y1,z0,z1):
    return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)).val()
def vol(s): return max(0,s.Volume()) if s.Solids() else 0.
def intersect_vol(a,b): return vol(a.intersect(b)) if a.Solids() and b.Solids() else 0.
def delta(a,b): return vol(a.cut(b))+vol(b.cut(a))
def load(folder,n): return cq.importers.importStep(str(folder/(n+'.step'))).val()
for style in ['precision','panelled']:
    out=ROOT/('prototype-l-'+style); checks=[]; failures=[]
    def check(label,ok):
        row={'check':label,'passed':bool(ok)}; checks.append(row)
        if not ok: failures.append(row)
    report=json.loads((out/'validation.json').read_text())
    for n,h in report['source_sha256'].items():
        check('L source unchanged '+n,hashlib.sha256((BASE/n).read_bytes()).hexdigest()==h)
    for n in ['validation.json','detail-validation.json']:
        check('L baseline report passes '+n,not json.loads((BASE/n).read_text())['failures'])
    for n in ['base','dpad','select','back','screen_shim','usb_c_collar']:
        check(n+' exact CAD preservation',delta(load(out,n),load(BASE,n))<1e-5)
    original=load(BASE,'lid')
    parts=[load(out,n) for n in ['lid','lid_inlay_dark','lid_inlay_orange']]
    joined=parts[0].fuse(*parts[1:]).clean()
    single=load(out,'lid_single_colour')
    check('Single-colour alternative equals complete multipart lid',delta(joined,single)<1e-5)
    check('No new obstruction anywhere in lid',vol(joined.cut(original))<1e-5)
    removed=original.cut(joined)
    check('All lid removal limited to outermost 0.24 mm',intersect_vol(removed,box(-30,110,-40,20,-20,7.01))<1e-5)
    if style=='precision': check('Precision assembled lid exactly equals L',delta(original,joined)<1e-5)
    check('Structural lid valid and connected',parts[0].isValid() and len(parts[0].Solids())==1)
    for a,b in itertools.combinations(parts,2): check('Colour bodies non-overlapping',vol(a.intersect(b))<1e-5)
    # Minimum 0.75 mm solid backing under all removed pockets and seam floors.
    for s in parts[1:]: check('Inlay has L backing',vol(s.translate((0,0,-.75)).cut(original))<1e-5)
    if style=='panelled': check('Seam floor has L backing',vol(removed.translate((0,0,-.75)).cut(original))<1e-5)
    for end in ['c','a']:
        n='usb_'+end+'_end_cap'; old=load(BASE,n); new=load(out,n)
        check(n+' valid connected',new.isValid() and len(new.Solids())==1)
        check(n+' no added obstruction',vol(new.cut(old))<1e-5)
        b=old.BoundingBox()
        check(n+' entire cavity and sidewalls preserved',intersect_vol(old.cut(new),box(-40,120,-40,20,b.zmin+.24,b.zmax-.24))<1e-5)
        if style=='panelled': check(n+' exact L geometry',delta(old,new)<1e-5)
        rolled=new.rotate((0,-10.6,1.575),(1,-10.6,1.575),180)
        check(n+' reversible',delta(new,rolled)<1e-5)
    c=load(out,'usb_c_end_cap').rotate((36.15,-10.6,0),(36.15,-10.6,1),180)
    check('Caps identical at either end',delta(c,load(out,'usb_a_end_cap'))<1e-5)
    for n in report['meshes']:
        mesh=trimesh.load(str(out/(n+'.stl')),force='mesh')
        check(n+' STL watertight and outward',mesh.is_watertight and mesh.is_winding_consistent and all(m.volume>0 for m in mesh.split()))
        s=load(out,n); solid_volume=s.Volume()
        check(n+' STL volume agrees with STEP',abs(mesh.volume-solid_volume)<max(.1,solid_volume*.002))
    # Every colour body must use the same face-down transform; do not trust volume alone.
    for n in ['lid','lid_inlay_dark','lid_inlay_orange']:
        s=load(out,n).rotate((0,0,0),(1,0,0),180).translate((2.5,3.9,7.25)); b=s.BoundingBox()
        mesh=trimesh.load(str(out/(n+'.stl')),force='mesh')
        expected=[[b.xmin,b.ymin,b.zmin],[b.xmax,b.ymax,b.zmax]]
        check(n+' common print registration',abs(mesh.bounds-expected).max()<.03)
    (out/'detail-validation.json').write_text(json.dumps({'checks':checks,'failures':failures,
        'basis':'Unchanged functional solids, no added lid/cap obstacles, preserved inner roof and cap sleeve. L movement checks inherited by geometric preservation.',
        'limitations':['No physical print or fatigue test performed. Multi-material adhesion and surface quality need a trial.']},indent=2)+'\n')
    print(style,len(checks),'checks;',len(failures),'failures',flush=True)
    assert not failures,failures
