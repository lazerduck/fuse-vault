from pathlib import Path
import json,zipfile,hashlib
R=Path(__file__).resolve().parent;O=R/'prototype-h'
for n in ['validation.json','closure-validation.json']:assert not json.loads((O/n).read_text())['failures'],n
manifest={'source_sha256':{n:hashlib.sha256((R/n).read_bytes()).hexdigest() for n in ['build_case_h.py','check_case_h_closure.py','render_case_h.py','package_case_h.py']},'baseline_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (R/'prototype-g').glob('*.step')}}
(O/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(O/'fuse-vault-h-print-pack.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(O.iterdir()):
  if p.suffix=='.zip':continue
  if p.suffix=='.stl':folder='SHIMS' if p.name.startswith('screen_shim') else ('SAMPLES' if 'coupon' in p.name else 'PRINT')
  elif p.suffix=='.step':folder='CAD'
  elif p.suffix=='.png':folder='PREVIEWS'
  elif p.suffix=='.json':folder='CHECKS'
  else:folder=''
  z.write(p,str(Path(folder)/p.name))
 for n in manifest['source_sha256']:z.write(R/n,'SOURCE/'+n)
print(O/'fuse-vault-h-print-pack.zip')
