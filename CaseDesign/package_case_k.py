"""Package K only after its geometry, assembly and cap checks pass."""
from pathlib import Path
import hashlib,json,shutil,zipfile
import trimesh
R=Path(__file__).resolve().parent;O=R/'prototype-k'
reports=['validation.json','closure-validation.json','detail-validation.json']
for n in reports:assert not json.loads((O/n).read_text())['failures'],n
files=['base','lid','dpad','select','back','usb_c_collar','usb_c_end_cap','usb_a_end_cap']
meshes={}
for n in files:
 p=O/(n+'.stl');m=trimesh.load(p,force='mesh')
 assert m.is_watertight and len(m.split())==1,n
 assert abs(m.bounds[0,2])<1e-5,(n,'not on bed')
 meshes[n]={'watertight':True,'one_body':True,'size_mm':m.extents.tolist(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
for n in ['usb_c_collar']:assert (O/(n+'.stl')).read_bytes()==(R/'prototype-f'/(n+'.stl')).read_bytes()
shutil.copyfile(O/'usb_c_end_cap.stl',O/'universal_end_cap.stl')
inputs=[R/n for n in ['build_case_k.py','check_case_k_details.py','check_case_f_closure.py','render_case_k.py','render_case_k_cutaway.py','package_case_k.py','pcb-print-model/source-board.step','pcb-print-model/measurements.json']]
inputs += [R/'prototype-f'/(n+'.step') for n in ['base','lid','dpad','select','back','usb_c_collar']]
manifest={'revision':'K','date':'2026-10-21','inputs_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'print_files':meshes,'cap_print_instruction':'Print two copies of universal_end_cap.stl; identical cap fits either end.','physical_fit_tested':False}
(O/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(O/'fuse-vault-case-k-print-pack.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in ['base','lid','dpad','select','back','universal_end_cap']:z.write(O/(n+'.stl'),'PRINT/'+n+'.stl')
 for n in ['usb_c_collar']:z.write(O/(n+'.stl'),'REUSE-OR-PRINT/'+n+'.stl')
 for n in files:z.write(O/(n+'.step'),'CAD/'+n+'.step')
 z.write(O/'case-assembly.step','CAD/case-assembly.step')
 for n in ['README.md','PRINT-AND-ASSEMBLY.txt','assembled.png','exploded.png','underside.png','contacts.png','dpad-cutaway.png','source-manifest.json',*reports]:z.write(O/n,n)
with zipfile.ZipFile(O/'fuse-vault-case-k-print-pack.zip') as z:assert z.testzip() is None
print('Packaged K: six new print files (print cap twice), reusable collar, CAD and checks')
