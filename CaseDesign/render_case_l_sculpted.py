"""Path-traced studio views of the exported CAD, with physical soft area lights.

Requires the existing CAD environment plus mitsuba (tested 3.9.1). Geometry is
read from STEP files named in validation.json. Display glass is a reference only;
no generated imagery, geometry substitutions, textures or fake detail are used.
"""
from pathlib import Path
import argparse
import json
import math
import tempfile
import cadquery as cq
import numpy as np
import trimesh
import mitsuba as mi

ROOT = Path(__file__).resolve().parent


def rgb_linear(rgb):
    return [c/12.92 if c <= .04045 else ((c+.055)/1.055)**2.4 for c in rgb]


def save_mesh(shape, filename):
    vertices, faces = shape.tessellate(.008, angularTolerance=.045)
    vertices = np.array([v.toTuple() for v in vertices], dtype=float)
    vertices = (vertices - np.array([36.15, -10.6, -5.9]))*.01
    mesh = trimesh.Trimesh(vertices=vertices, faces=np.array(faces), process=True)
    # Duplicate vertices only at actual sharp boundaries; retain smooth normals
    # across finely tessellated curved CAD faces. This does not alter the mesh.
    mesh = trimesh.graph.smooth_shade(mesh, angle=.3, facet_minarea=None)
    mesh.export(str(filename), include_attributes=True)


def plastic(color, roughness=.19):
    return {'type':'roughplastic', 'distribution':'ggx', 'alpha':roughness,
            'diffuse_reflectance':{'type':'rgb','value':rgb_linear(color)},
            'int_ior':1.46, 'ext_ior':1.0, 'nonlinear':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder',default='prototype-l-sculpted')
    parser.add_argument('--spp',type=int,default=192)
    parser.add_argument('--width',type=int,default=1800)
    parser.add_argument('--views',nargs='+',default=['studio','top-studio','detail-studio'])
    args=parser.parse_args()
    folder=ROOT/args.folder
    colors=json.loads((folder/'validation.json').read_text())['colors']
    # LLVM CPU backend uses SIMD/JIT; scalar fallback remains available.
    try:
        mi.set_variant('llvm_ad_rgb')
    except Exception:
        mi.set_variant('scalar_rgb')
    T=mi.ScalarTransform4f
    with tempfile.TemporaryDirectory(prefix='fuse-vault-render-') as temp:
        scene_base={'type':'scene','integrator':{'type':'path','max_depth':8,'rr_depth':5},
                    'environment':{'type':'constant','radiance':{'type':'rgb','value':[.35,.35,.35]}}}
        for name,color in colors.items():
            shape=cq.importers.importStep(str(folder/(name+'.step'))).val()
            path=Path(temp)/(name+'.ply'); save_mesh(shape,path)
            scene_base['part_'+name]={'type':'ply','filename':str(path),'bsdf':plastic(color)}
        # Same documented display envelope used in the CAD preview. Dark glass
        # helps show the unchanged bezel opening without inventing a screen UI.
        display=cq.Workplane('XY').box(27.9,13.5,1.5,centered=False).translate((17.10146,-18.49208,4.1)).val()
        path=Path(temp)/'display_reference.ply'; save_mesh(display,path)
        scene_base['display_reference']={'type':'ply','filename':str(path),
            'bsdf':{'type':'roughplastic','alpha':.012,'int_ior':1.5,'ext_ior':1.,
                    'specular_reflectance':{'type':'rgb','value':[.36,.38,.40]},
                    'diffuse_reflectance':{'type':'rgb','value':[.001,.002,.003]}}}
        scene_base['floor']={'type':'rectangle','to_world':T.translate([0,0,-.001]).scale([15,15,15]),
                             'bsdf':{'type':'diffuse','reflectance':{'type':'rgb','value':[.64,.63,.61]}}}
        lights=[('key',[-.8,-.8,1.8],[.85,.65,1.],[5.0,4.85,4.65]),
                ('fill',[1.1,-.2,1.1],[.7,.55,1.],[2.0,2.1,2.25]),
                ('rim',[.0,1.1,1.6],[.9,.35,1.],[3.1,3.2,3.4])]
        for name,origin,size,radiance in lights:
            scene_base[name]={'type':'rectangle',
                'to_world':T.look_at(origin=origin,target=[0,0,.05],up=[0,1,0]).scale(size),
                'emitter':{'type':'area','radiance':{'type':'rgb','value':radiance}}}
        settings={
            'studio':dict(origin=[1.08,-1.64,1.78],target=[0,0,.06],up=[0,0,1],fov=30,ratio=1.65),
            'top-studio':dict(origin=[0,0,2.6],target=[0,0,.06],up=[0,1,0],fov=30,ratio=1.65),
            'detail-studio':dict(origin=[.30,-.80,.94],target=[-.18,-.015,.11],up=[0,0,1],fov=23,ratio=1.5),
        }
        for view in args.views:
            conf=settings[view]; width=args.width; height=round(width/conf['ratio'])
            scene=dict(scene_base)
            scene['sensor']={'type':'perspective','fov':conf['fov'],'fov_axis':'x',
                'to_world':T.look_at(origin=conf['origin'],target=conf['target'],up=conf['up']),
                'sampler':{'type':'independent','sample_count':args.spp},
                'film':{'type':'hdrfilm','width':width,'height':height,'rfilter':{'type':'tent'},
                        'component_format':'float32','pixel_format':'rgb'}}
            loaded=mi.load_dict(scene)
            print('Rendering',view,width,height,args.spp,'spp',flush=True)
            result=mi.render(loaded,spp=args.spp,seed=47)
            mi.util.write_bitmap(str(folder/(view+'.png')),result)
            print('Wrote',folder/(view+'.png'),flush=True)
    (folder/'render-settings.json').write_text(json.dumps({
        'renderer':'Mitsuba','version':mi.__version__,'variant':mi.variant(),
        'samples_per_pixel':args.spp,'width_px':args.width,'views':args.views,
        'geometry':'Exported STEP shapes from validation.json colors; no geometric edits',
        'materials':'Rough plastic, sRGB CAD colours converted to linear reflectance',
        'lighting':'Three rectangular area lights and constant fill; diffuse studio floor',
        'display':'Reference glass envelope only; same as CAD renderer',
        'limitations':['Illustrative material roughness and studio lighting; surface finish is not a print prediction.']},indent=2)+'\n')

if __name__=='__main__':
    main()
