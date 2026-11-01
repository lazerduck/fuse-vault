"""Standard multipart 3MF lid + print pack; no slicer profile or G-code."""
from pathlib import Path
import hashlib,json,zipfile,sys
import xml.etree.ElementTree as ET
import trimesh
ROOT=Path(__file__).resolve().parent
NS='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
ET.register_namespace('',NS)
def sub(parent,tag,attrs=None): return ET.SubElement(parent,'{'+NS+'}'+tag,attrs or {})
for style in (sys.argv[1:] or ['precision','panelled']):
    out=ROOT/('prototype-l-'+style)
    report=json.loads((out/'validation.json').read_text())
    independent=json.loads((out/'detail-validation.json').read_text())
    assert not report['failures'] and not independent['failures']
    if style=='sculpted':
        assert independent.get('export_sha256'), 'Independent evidence must identify checked exports'
        for name,digest in independent['export_sha256'].items():
            assert hashlib.sha256((out/name).read_bytes()).hexdigest()==digest, 'Stale checks: '+name
    model=ET.Element('{'+NS+'}model',{'unit':'millimeter'})
    res=sub(model,'resources'); materials=sub(res,'basematerials',{'id':'1'})
    names=list(report['ams_groups']) if 'ams_groups' in report else ['lid','lid_inlay_dark','lid_inlay_orange']
    for name in names:
        rgb=report.get('ams_colors',report['colors'])[name]
        sub(materials,'base',{'name':name,'displaycolor':'#'+''.join(f'{round(c*255):02X}' for c in rgb)+'FF'})
    expected=[]
    for i,name in enumerate(names):
        mesh=trimesh.load(str(out/(name+'.stl')),force='mesh'); assert mesh.is_watertight
        expected.append(mesh)
        obj=sub(res,'object',{'id':str(i+2),'type':'model','name':name,'pid':'1','pindex':str(i)})
        m=sub(obj,'mesh'); vertices=sub(m,'vertices'); triangles=sub(m,'triangles')
        for x,y,z in mesh.vertices: sub(vertices,'vertex',dict(zip(['x','y','z'],map(str,[x,y,z]))))
        for a,b,c in mesh.faces: sub(triangles,'triangle',dict(zip(['v1','v2','v3'],map(str,[a,b,c]))))
    obj=sub(res,'object',{'id':'5','type':'model','name':'L '+style+' multipart lid'})
    components=sub(obj,'components')
    for i in range(2,5): sub(components,'component',{'objectid':str(i)})
    sub(sub(model,'build'),'item',{'objectid':'5'})
    with zipfile.ZipFile(out/'lid-ams.3mf','w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml','<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
        z.writestr('_rels/.rels','<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
        z.writestr('3D/3dmodel.model',ET.tostring(model,encoding='utf-8',xml_declaration=True))
    with zipfile.ZipFile(out/'lid-ams.3mf') as z: decoded=ET.fromstring(z.read('3D/3dmodel.model'))
    meshes=[]
    for obj in decoded.findall('{'+NS+'}resources/{'+NS+'}object'):
        m=obj.find('{'+NS+'}mesh')
        if m is None: continue
        vertices=[[float(v.attrib[k]) for k in ['x','y','z']] for v in m.find('{'+NS+'}vertices')]
        faces=[[int(t.attrib[k]) for k in ['v1','v2','v3']] for t in m.find('{'+NS+'}triangles')]
        meshes.append(trimesh.Trimesh(vertices=vertices,faces=faces))
    assert len(meshes)==3
    for actual,original in zip(meshes,expected):
        assert actual.is_watertight and len(actual.faces)==len(original.faces)
        assert abs(actual.volume-original.volume)<1e-5
        assert abs(actual.bounds-original.bounds).max()<1e-6
    files=[p for p in sorted(out.iterdir()) if p.suffix in ['.stl','.step','.3mf','.png','.md','.txt','.json'] and p.name!='source-manifest.json']
    script_names=['build_case_l_styles.py','check_case_l_styles.py','render_case_l_styles.py','package_case_l_styles.py']
    if style=='sculpted':
        script_names += ['build_case_l_sculpted.py','check_case_l_sculpted.py','render_case_l_sculpted.py','render_case_l_sculpted_section.py']
    if style=='panelled-v2':
        script_names += ['build_case_l_panelled_v2.py','check_case_l_panelled_v2.py']
    scripts=[ROOT/n for n in script_names]
    manifest={'style':style,'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              'scripts':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in scripts},
              '3mf_roundtrip':'three watertight meshes; bounds, face counts and volumes match',
              'print_counts':{'universal_end_cap.stl':2},'physical_trial':'pending'}
    (out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with zipfile.ZipFile(out/('fuse-vault-l-'+style+'-print-pack.zip'),'w',zipfile.ZIP_DEFLATED) as z:
        for p in files+[out/'source-manifest.json']: z.write(p,p.name)
        for p in scripts: z.write(p,'source/'+p.name)
    print('Packaged',style,flush=True)
