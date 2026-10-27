"""Package validated I exports; run after the build, detail and closure checks."""
from pathlib import Path
import hashlib,json,zipfile
import trimesh
R=Path(__file__).resolve().parent;O=R/'prototype-i'
reports=['validation.json','closure-validation.json','detail-validation.json']
for n in reports:
 assert not json.loads((O/n).read_text())['failures'],n
files=['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']
meshes={}
for n in files:
 p=O/(n+'.stl');m=trimesh.load(p,force='mesh')
 assert m.is_watertight and len(m.split())==1,n
 assert abs(m.bounds[0,2])<1e-5,(n,'not on bed')
 meshes[n]={'watertight':True,'one_body':True,'size_mm':m.extents.tolist(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
for n in ['select','back','usb_c_collar']:assert (O/(n+'.stl')).read_bytes()==(R/'prototype-f'/(n+'.stl')).read_bytes()
inputs=[R/'build_case_i.py',R/'check_case_i_details.py',R/'check_case_f_closure.py',R/'render_case_f.py',R/'package_case_i.py',R/'pcb-print-model/source-board.step',R/'pcb-print-model/measurements.json']
inputs += [R/'prototype-f'/(n+'.step') for n in ['base','lid','dpad','select','back','usb_c_collar']]
manifest={'revision':'I','date':'2026-10-21','inputs_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'print_files':meshes,'physical_fit_tested':False}
(O/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(O/'fuse-vault-case-i-print-pack.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in files:
  z.write(O/(n+'.stl'),'PRINT/'+n+'.stl')
  z.write(O/(n+'.step'),'CAD/'+n+'.step')
 z.write(O/'case-assembly.step','CAD/case-assembly.step')
 for n in ['README.md','PRINT-AND-ASSEMBLY.txt','assembled.png','exploded.png','underside.png','source-manifest.json',*reports]:z.write(O/n,n)
with zipfile.ZipFile(O/'fuse-vault-case-i-print-pack.zip') as z:assert z.testzip() is None
print('Packaged eight watertight, single-body, bed-oriented STLs and editable CAD')
