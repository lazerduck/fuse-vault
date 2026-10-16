"""Package validated CAD and a standard multipart 3MF (no printer settings)."""
from pathlib import Path
import json, hashlib, zipfile
import xml.etree.ElementTree as ET
import trimesh
R=Path(__file__).resolve().parent;O=R/'prototype-g-panelled'
for name in ['validation.json','closure-validation.json']:
 assert not json.loads((O/name).read_text())['failures'],name
ns='http://schemas.microsoft.com/3dmanufacturing/core/2015/02';ET.register_namespace('',ns)
def el(tag,*args,**kwargs):return ET.Element('{'+ns+'}'+tag,*args,**kwargs)
def sub(parent,tag,attrs={}):child=el(tag,attrs);parent.append(child);return child
model=el('model',{'unit':'millimeter','{http://www.w3.org/XML/1998/namespace}lang':'en-US'})
sub(model,'metadata',{'name':'Title'}).text='Fuse Vault G panelled — AMS lid'
resources=sub(model,'resources');materials=sub(resources,'basematerials',{'id':'1'})
for name,col in [('Charcoal','#1F2229FF'),('Pale grey','#C7C9C4FF'),('Orange','#FF6E06FF')]:sub(materials,'base',{'name':name,'displaycolor':col})
names=['lid','lid_inlay_grey','lid_inlay_orange'];expected=[]
for i,name in enumerate(names):
 mesh=trimesh.load(str(O/(name+'.stl')),force='mesh');assert mesh.is_watertight;expected.append(len(mesh.faces))
 obj=sub(resources,'object',{'id':str(i+2),'type':'model','name':name,'pid':'1','pindex':str(i)})
 m=sub(obj,'mesh');v=sub(m,'vertices');t=sub(m,'triangles')
 for x,y,z in mesh.vertices:sub(v,'vertex',{'x':str(x),'y':str(y),'z':str(z)})
 for a,b,c in mesh.faces:sub(t,'triangle',{'v1':str(a),'v2':str(b),'v3':str(c)})
obj=sub(resources,'object',{'id':'5','type':'model','name':'Fuse Vault G panelled lid'});components=sub(obj,'components')
for i in range(2,5):sub(components,'component',{'objectid':str(i)})
sub(sub(model,'build'),'item',{'objectid':'5'})
model_bytes=ET.tostring(model,encoding='utf-8',xml_declaration=True)
with zipfile.ZipFile(O/'lid-ams.3mf','w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('[Content_Types].xml','<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
 z.writestr('_rels/.rels','<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
 z.writestr('3D/3dmodel.model',model_bytes)
# Independently read the zipped XML back and reconstruct its meshes.
with zipfile.ZipFile(O/'lid-ams.3mf') as z:root=ET.fromstring(z.read('3D/3dmodel.model'))
meshes=[]
for obj in root.findall('{'+ns+'}resources/{'+ns+'}object'):
 m=obj.find('{'+ns+'}mesh')
 if m is None:continue
 verts=[[float(v.attrib[k]) for k in ['x','y','z']] for v in m.find('{'+ns+'}vertices')]
 faces=[[int(t.attrib[k]) for k in ['v1','v2','v3']] for t in m.find('{'+ns+'}triangles')]
 meshes.append(trimesh.Trimesh(vertices=verts,faces=faces))
assert len(meshes)==3
assert [len(m.faces) for m in meshes]==expected
assert all(m.is_watertight for m in meshes)
manifest={'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'build_panelled_g.py',R/'check_panelled_g_closure.py',R/'render_panelled_g.py',R/'package_panelled_g.py',*(R/'prototype-g').glob('*.step')]},'multipart_3mf':{'parts':3,'roundtrip_meshes_watertight':True,'printer_profile':None}}
(O/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(O/'fuse-vault-g-panelled-print-pack.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(O.iterdir()):
  if p.is_file() and p.suffix!='.zip':z.write(p,p.name)
 for n in ['build_panelled_g.py','check_panelled_g_closure.py','render_panelled_g.py','package_panelled_g.py']:z.write(R/n,'source/'+n)
print('Packaged',O/'fuse-vault-g-panelled-print-pack.zip')
