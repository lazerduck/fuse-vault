"""Package only a fully validated revision L print set."""
from pathlib import Path
import hashlib,json,zipfile
R=Path(__file__).resolve().parent;O=R/'prototype-l'
for name in ['validation.json','detail-validation.json']:
 report=json.loads((O/name).read_text());assert not report['failures'],name
main=json.loads((O/'validation.json').read_text())
assert len(main.get('meshes',{}))==9,'Print exports incomplete'
assert main['cap_nominal_wall_flex_per_side_mm']==.4,'Wrong cap revision'
details=json.loads((O/'detail-validation.json').read_text())
for end in ['c','a']:
 assert max(row['intentional_bead_interference_mm3'] for row in details['cap_flex_samples'] if row['end']==end)>1e-4,('missing cap retention',end)
files=['base.stl','lid.stl','dpad.stl','select.stl','back.stl','screen_shim.stl','universal_end_cap.stl','usb_c_collar.stl','PRINT-AND-ASSEMBLY.txt','README.md','assembled.png','exploded.png','contacts.png','validation.json','detail-validation.json']
manifest={'revision':'L','files':{n:hashlib.sha256((O/n).read_bytes()).hexdigest() for n in files},'sources':{n:hashlib.sha256((R/n).read_bytes()).hexdigest() for n in ['build_case_l.py','check_case_l_details.py','render_case_l.py','package_case_l.py']},'print_counts':{'universal_end_cap.stl':2},'default_material':'PLA','physical_trial':'User confirmed assembly fits well on 2026-10-22; remaining button feel suspected switch-quality limited, unconfirmed.'}
(O/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(O/'fuse-vault-case-l-print-pack.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in files+['source-manifest.json']:z.write(O/n,n)
print(O/'fuse-vault-case-l-print-pack.zip')
