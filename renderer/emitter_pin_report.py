#!/usr/bin/env python3
"""Which reviewed emitter profiles (outdoor-light-profiles.json) apply to a client's own models.

Per profile, the model the client resolves (game archive order, client_archives.py) is:
  matched   its sha256 is pinned (the profile's own asset or one of its variants), so
            world_lights_builder places that pin's reviewed emitter;
  variant   the path exists with an unpinned sha256: the builder drops the profile and logs an error
            ("Emitter profile asset changed"), unless the model has a supported native light;
  absent    no archive holds the path, so no placement can use the profile.
With --batches, every model that is not the profile's own asset also gets its skin batches: texture
name, render flags and blend mode, bounds of the batch's triangles and their center (rounded to
3 decimals), and the texture's colour. That is how the 0.3.110 positions were authored ("installed
luminous skin-batch bounds center: <texture>"; batches() reproduces all 269 models of the 0.3.110
evidence, kept as digests in tests/fixtures/lamp-batches-0.3.110.sha256.json), and how a variant's
position is derived.
Reads MPQs only; no game, no Wine. The report holds paths, hashes, archive names and derived
coordinates and colours, no game bytes.
"""
from __future__ import annotations
import argparse,collections,hashlib,json,sys
from pathlib import Path
from world_scene_builder import Assets,array,decode_blp,skin_vertex_indices,srgb_to_linear,string,unpack
import client_archives
import outdoor_light_profiles

def texture_colour(assets,path):
    """Mean sRGB of a texture, weighted by alpha times brightness (the glowing texels), normalised to
    a maximum of 1, and its linear-light equivalent (the space of the profile rgb)."""
    try:w,h,rgba=decode_blp(assets.read(path),64)
    except (ValueError,FileNotFoundError) as exc:return {'error':str(exc)}
    total=[0.,0.,0.];weight=0.
    for i in range(0,len(rgba),4):
        r,g,b,a=rgba[i:i+4];wt=a*(r+g+b)
        total=[total[0]+r*wt,total[1]+g*wt,total[2]+b*wt];weight+=wt
    if not weight:return {'error':'no visible texels'}
    top=max(total);srgb=[v/top for v in total]
    return {'srgb':[round(v,2) for v in srgb],'linear':[round(srgb_to_linear(v),2) for v in srgb]}

def batches(assets,path):
    """Skin batches of an M2 (skin 00): texture, (render flags, blend mode), triangle bounds, center."""
    b=assets.read(path);skin=assets.read(path[:-3]+'00.skin')
    count,offset=unpack('<II',b,60);vertices=[v[:3] for v in array(b,offset,count,'<3f4B4B3f4f')]
    indices=skin_vertex_indices(b,skin)
    n,off=unpack('<II',skin,28);subsets=array(skin,off,n,'<10H7f')
    n,off=unpack('<II',skin,36);skin_batches=array(skin,off,n,'<2B11H')
    n,off=unpack('<II',b,80);textures=array(b,off,n,'<4I')
    n,off=unpack('<II',b,112);flags=array(b,off,n,'<2H')
    n,off=unpack('<II',b,128);tex_lookup=[x[0] for x in array(b,off,n,'<H')]
    result=[]
    for batch in skin_batches:
        if batch[3]>=len(subsets) or batch[6]>=len(flags) or batch[9]>=len(tex_lookup) or tex_lookup[batch[9]]>=len(textures):
            raise ValueError('SKIN material exceeds winning M2: '+path)
        sub=subsets[batch[3]];first=sub[4]+(sub[1]<<16)
        if not sub[5] or sub[5]%3 or first+sub[5]>len(indices):raise ValueError('Invalid SKIN batch triangle range: '+path)
        points=[vertices[i] for i in indices[first:first+sub[5]]]
        lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
        texture=textures[tex_lookup[batch[9]]]
        result.append({'texture':string(b,texture[3]) if texture[0]==0 and texture[2] else f'<type {texture[0]}>',
                       'flags':list(flags[batch[6]]),'center':[round((lo[i]+hi[i])/2,3) for i in range(3)],'bounds':[lo,hi]})
    return result

def report(assets,catalog=None,catalog_path=None,with_batches=False):
    catalog=catalog if catalog is not None else outdoor_light_profiles.load(catalog_path)
    profiles={}
    for path,p in sorted(catalog.items()):
        pinned=list(outdoor_light_profiles.pinned(p))
        if path not in assets.providers:
            profiles[path]={'status':'absent','pinned':pinned};continue
        sha=hashlib.sha256(assets.read(path)).hexdigest()
        profiles[path]={'status':'matched' if sha in pinned else 'variant','asset_sha256':sha,
                        'archive':assets.origin(path),'pinned':pinned}
        if sha in pinned:profiles[path]['pin']='profile' if sha==pinned[0] else p['variants'][sha].get('asset','variant')
        if with_batches:
            try:profiles[path]['batches']=batches(assets,path)
            except (ValueError,IndexError,FileNotFoundError) as exc:
                profiles[path]['geometry_error']=str(exc)
                profiles[path]['skin_archive']=assets.origin(path[:-3]+'00.skin')
                continue
            for x in profiles[path]['batches']:x['texture_colour']=texture_colour(assets,x['texture'])
    counts=collections.Counter(x['status'] for x in profiles.values())
    return {'archive_fingerprint':assets.fingerprint(),'profiles_total':len(profiles),
            'summary':{s:counts.get(s,0) for s in ('matched','variant','absent')},'profiles':profiles}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__.split('\n')[0])
    client_archives.add_arguments(ap)
    ap.add_argument('--catalog',type=Path,help='default: outdoor-light-profiles.json beside this script')
    ap.add_argument('--output',type=Path,help='write the JSON report here (default: stdout)')
    ap.add_argument('--batches',action='store_true',help="add skin-batch geometry for models that are not the profile's own asset")
    ap.add_argument('--require-all',action='store_true',help='exit 1 unless every profile is matched')
    args=ap.parse_args();assets=Assets.from_args(args)
    try:result=report(assets,catalog_path=args.catalog,with_batches=args.batches)
    finally:assets.close()
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    else:sys.stdout.write(text)
    for path,x in result['profiles'].items():
        if x['status']!='matched':print(f"{x['status']:8} {path} {x.get('asset_sha256','')} {x.get('archive','')}",file=sys.stderr)
    print('emitter pins:',json.dumps(result['summary']),file=sys.stderr)
    if args.require_all and result['summary']['matched']!=result['profiles_total']:raise SystemExit(1)
