#!/usr/bin/env python3
"""Stage point-only transmission coverage beside an existing world-light cache.

Ordinary FGS2/FGS3 files, separate from native sun/GI caster descriptors. Only
constant, literal, depth-writing blend-2 physical surfaces with bounded UVs qualify.
The point-shadow worker decides when to load them; no runtime MPQ scanning.
"""
import argparse
import collections
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import client_archives
from world_scene_builder import Assets, Builder, array, decode_blp, matvec, string, unpack
from world_lights_builder import constant_track, static_bone_offset

INSTANCE = struct.Struct('<32sQI18f')
MAX_VERTICES = 65536
MAX_TRIANGLES = 8192
MAX_MODEL_BYTES = 16*1024*1024
MAX_TOTAL_BYTES = 256*1024*1024
MAX_MODELS = 4096


def bayer8():
    matrix = [[0]]
    while len(matrix) < 8:
        n = len(matrix)
        matrix = [[4*matrix[y % n][x % n] + ((0,2),(3,1))[y//n][x//n]
                   for x in range(n*2)] for y in range(n*2)]
    return matrix


BAYER = bayer8()


def coverage_texture(width, height, rgba, opacity, used=None):
    """Stable binary coverage at original mip dimensions, never upsampled.

    Select a deterministic Bayer phase whose global and used-UV coverage means
    differ from opacity × actual texture alpha by at most .02. RGB and UV repeat
    phase stay native. This is area coverage, not exact per-texel transmission.
    """
    if not 8 <= width <= 128 or not 8 <= height <= 128 or len(rgba) != width*height*4:
        raise ValueError('Transmission texture must fit the bounded 8..128px mip')
    if not math.isfinite(opacity) or not 0 < opacity <= 1:
        raise ValueError('Transmission requires bounded constant opacity')
    used = set(range(width*height)) if used is None else used
    if not used or not all(0 <= i < width*height for i in used):
        raise ValueError('Transmission used UV domain is empty/invalid')
    coverage = [opacity*a/255 for a in rgba[3::4]]
    expected = sum(coverage)/len(coverage); expected_used = sum(coverage[i] for i in used)/len(used)
    best = None
    for phase_y,phase_x in itertools.product(range(8),repeat=2):
        alpha = [255 if value >= (BAYER[(i//width+phase_y)&7][(i%width+phase_x)&7]+.5)/64 else 0
                 for i,value in enumerate(coverage)]
        error = max(abs(sum(alpha)/255/len(alpha)-expected),abs(sum(alpha[i] for i in used)/255/len(used)-expected_used))
        if best is None or error < best[0]:
            best = error,alpha
        if error <= .02:
            break
    if best[0] > .02:
        raise ValueError('Transmission dither cannot preserve global/used UV coverage within .02')
    output = bytearray(rgba); output[3::4] = bytes(best[1])
    return width,height,bytes(output)


def uv_texels(vertices, faces, width, height):
    """Rasterize actual triangle UVs to a bounded union of source-mip texels."""
    selected = set(); work = 0
    for triangle in faces:
        a,b,c = [vertices[i][14:16] for i in triangle]
        determinant = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if abs(determinant) < 1e-12:
            continue
        lo_x,hi_x = math.floor(min(a[0],b[0],c[0])*width),math.ceil(max(a[0],b[0],c[0])*width)
        lo_y,hi_y = math.floor(min(a[1],b[1],c[1])*height),math.ceil(max(a[1],b[1],c[1])*height)
        work += (hi_x-lo_x)*(hi_y-lo_y)
        if work > 1000000:
            raise ValueError('Transmission UV rasterization bound')
        for y in range(lo_y,hi_y):
            for x in range(lo_x,hi_x):
                u,v = (x+.5)/width-a[0],(y+.5)/height-a[1]
                q = (u*(c[1]-a[1])-v*(c[0]-a[0]))/determinant
                r = ((b[0]-a[0])*v-(b[1]-a[1])*u)/determinant
                if q >= -1e-8 and r >= -1e-8 and q+r <= 1+1e-8:
                    selected.add((y%height)*width+x%width)
    return selected


def _array(blob, header, fmt):
    count, offset = unpack('<II',blob,header)
    return array(blob,offset,count,fmt)


def physical_surface(flags, unit):
    """Depth-writing alpha surfaces; additive/Mod2X/unlit/overlay are not blockers."""
    material, blend = flags
    return blend == 2 and bool(material & 0x10) and not material & (1 | 0x40) and not unit[0] & 4 and unit[2] != 0x8000 and unit[7] == 0


def model(assets, path):
    blob = assets.read(path)
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I',blob,4)[0] != 264:
        raise ValueError('Transmission requires MD20/v264')
    flags = _array(blob,112,'<2H')
    if not any(blend == 2 and flag & 0x10 and not flag & (1 | 0x40) for flag,blend in flags):
        return None
    if unpack('<I',blob,60)[0] > MAX_VERTICES:
        raise ValueError('Transmission source vertex bound')
    skin = assets.read(path[:-3]+'00.skin')
    if skin[:4] != b'SKIN':
        raise ValueError('Transmission requires a WotLK skin')
    if unpack('<I',skin,12)[0] > MAX_TRIANGLES*3:
        raise ValueError('Transmission source triangle bound')
    vertices = _array(blob,60,'<3f4B4B3f4f')
    lookup = [v[0] for v in _array(skin,4,'<H')]
    indices = [lookup[v[0]] for v in _array(skin,12,'<H')]
    subsets = _array(skin,28,'<10H7f'); units = _array(skin,36,'<2B11H')
    textures = _array(blob,80,'<4I'); texlookup = _array(blob,128,'<H')
    opacitylookup = _array(blob,144,'<h'); uvlookup = _array(blob,152,'<h')
    no, oo = unpack('<II',blob,88); nc, oc = unpack('<II',blob,72)
    # Use the production native-caster policy as the duplicate oracle. Its
    # vertices retain original M2 indices, so this compares actual faces.
    native = {tuple(sorted(t[:3])) for t in Builder(assets).m2(path)[1]}
    out_vertices, triangles, materials, evidence, refusals = [], [], [], [], collections.Counter()
    positions = {}; remap = {}; selected = set()
    for batch_index, unit in enumerate(units):
        if not physical_surface(flags[unit[6]],unit):
            continue
        try:
            if unit[8] != 1:
                raise ValueError('Multitexture transmission surface')
            texture = textures[texlookup[unit[9]][0]]
            if texture[0] != 0 or not texture[2]:
                raise ValueError('Dynamic transmission texture replacement')
            if unit[12] != 65535 and uvlookup[unit[12]][0] != -1:
                raise ValueError('Transmission UV transform is unsupported')
            opacity = 1.
            if unit[5] != 65535:
                if unit[5] >= nc:
                    raise ValueError('Transmission colour index')
                opacity *= constant_track(blob,oc+unit[5]*40+20,'<h',(32767,))[0]/32767
            if unit[11] != 65535:
                index = opacitylookup[unit[11]][0]
                if index != -1:
                    if not 0 <= index < no:
                        raise ValueError('Transmission weight index')
                    opacity *= constant_track(blob,oo+index*20,'<h',(32767,))[0]/32767
            if opacity == 0:
                continue
            if not 0 < opacity <= 1:
                raise ValueError('Physical blend2 opacity is outside bounded policy')
            subset = subsets[unit[3]]; first, size = subset[4]+(subset[1]<<16), subset[5]
            if first+size > len(indices) or size%3:
                raise ValueError('Transmission triangle range')
            faces = [tuple(indices[i:i+3]) for i in range(first,first+size,3)]
            faces = [t for t in faces if tuple(sorted(t)) not in native and tuple(sorted(t)) not in selected]
            if not faces:
                continue
            if len(triangles)+len(faces) > MAX_TRIANGLES:
                raise ValueError('Transmission model triangle bound')
            used = {i for t in faces for i in t}
            # FGS2 samples wrap. A source clamp axis is representable only
            # inside its unit UV interval; never fabricate repeated geometry.
            for axis,bit in ((14,1),(15,2)):
                if not texture[1] & bit and any(not 0 <= vertices[i][axis] <= 1 for i in used):
                    raise ValueError('Clamped transmission UV outside unit interval')
            # Prove the entire batch before adding any of its triangles.
            for i in used:
                vertex = vertices[i]; shift = [0.,0.,0.]; total = sum(vertex[3:7])
                for weight, bone in zip(vertex[3:7],vertex[7:11]):
                    if weight:
                        translation = static_bone_offset(blob,bone)
                        for k in range(3):
                            shift[k] += translation[k]*weight/total
                position = tuple(vertex[k]+shift[k] for k in range(3))
                if not all(math.isfinite(v) and abs(v) <= 100000 for v in (*position,*vertex[11:16])):
                    raise ValueError('Transmission nonfinite/out-of-range vertex')
                positions[i] = (*position,*vertex[11:16])
            name = string(blob,texture[3]); width,height,rgba = decode_blp(assets.read(name),128)
            used_uv = uv_texels(vertices,faces,width,height)
            w,h,mask = coverage_texture(width,height,rgba,opacity,used_uv)
            if not any(mask[3::4]):
                continue
            if sum(len(m[4]) for m in materials)+len(mask) > MAX_MODEL_BYTES:
                raise ValueError('Transmission model texture bound')
            material = len(materials); materials.append(((.5,.5,.5),w,h,.5,mask))
            for t in faces:
                added = []
                for i in t:
                    if i not in remap:
                        remap[i] = len(out_vertices); out_vertices.append(positions[i])
                    added.append(remap[i])
                triangles.append((*added,material)); selected.add(tuple(sorted(t)))
            evidence.append({'batch':batch_index, 'texture':name, 'texture_sha256':hashlib.sha256(assets.read(name)).hexdigest(),
                             'opacity':opacity, 'triangles':len(faces), 'source_texture_size':[width,height],
                             'source_texture_wrap_flags':texture[1]&3,
                             'mask_size':[w,h], 'mean_coverage':sum(mask[3::4])/255/(w*h),
                             'expected_mean_coverage':opacity*sum(rgba[3::4])/255/(width*height),
                             'used_uv_texels':len(used_uv),
                             'used_uv_mean_coverage':sum(mask[i*4+3] for i in used_uv)/255/len(used_uv),
                             'expected_used_uv_coverage':opacity*sum(rgba[i*4+3] for i in used_uv)/255/len(used_uv)})
        except (ValueError,IndexError,FileNotFoundError,struct.error) as exc:
            refusals[str(exc)] += 1
    if not triangles:
        if refusals:
            raise ValueError('Unsupported transmission evidence: '+json.dumps(refusals,sort_keys=True))
        return None
    return {'vertices':out_vertices, 'triangles':triangles, 'materials':materials, 'evidence':evidence,
            'refusals':dict(refusals), 'asset_sha256':hashlib.sha256(blob).hexdigest(),
            'skin_sha256':hashlib.sha256(skin).hexdigest(), 'archive':assets.origin(path)}


def encode_fgs(mesh):
    result = bytearray(struct.pack('<4s4I',b'FGS2',2,len(mesh['vertices']),len(mesh['triangles']),len(mesh['materials'])))
    for vertex in mesh['vertices']:
        result.extend(struct.pack('<8f',*vertex))
    for triangle in mesh['triangles']:
        result.extend(struct.pack('<4I',*triangle))
    for rgb,w,h,cutoff,rgba in mesh['materials']:
        result.extend(struct.pack('<3fIIfI',*rgb,w,h,cutoff,len(rgba))); result.extend(rgba)
    if len(result) > MAX_MODEL_BYTES:
        raise ValueError('Encoded transmission model byte bound')
    return bytes(result)


def build(cacheRoot, lightsOutput, maps, assets):
    cacheRoot, lightsOutput = Path(cacheRoot), Path(lightsOutput)
    destination = lightsOutput/'point-transmission'
    if destination.exists():
        raise FileExistsError('Transmission destination already exists: '+str(destination))
    paths = {hashlib.sha256(('FGS3-v1:'+p).encode()).digest():p for p in assets.providers if p.endswith('.m2')}
    meshes, model_blobs, descriptors, skipped, inputs = {}, {}, {}, {}, {}
    counts = collections.Counter(models=0,placements=0,unknown_m2_keys=0); total = 0
    for mapname in dict.fromkeys(maps):
        if not mapname or any(c in mapname for c in '/\\') or mapname in ('.','..'):
            raise ValueError('Noncanonical transmission map directory')
        for tile in sorted((cacheRoot/mapname).glob('*.fg3')):
            if tile.stat().st_size > 128*1024*1024:
                raise ValueError('Source FGS3 exceeds bounded 128 MiB file size')
            blob = tile.read_bytes(); magic,version,count = unpack('<4sII',blob)
            if magic != b'FGS3' or version != 3 or count > 1000000 or len(blob) != 12+count*INSTANCE.size:
                raise ValueError('Invalid source FGS3: '+str(tile))
            inputs[mapname+'/'+tile.name] = hashlib.sha256(blob).hexdigest(); records = []; seen = set()
            for entry in INSTANCE.iter_unpack(blob[12:]):
                key,uid,category = entry[:3]
                if category not in (1,3) or (uid,category) in seen:
                    continue
                seen.add((uid,category)); path = paths.get(key)
                if path is None:
                    counts['unknown_m2_keys'] += 1; continue
                if path not in meshes:
                    try:
                        mesh = model(assets,path); meshes[path] = mesh
                        if mesh:
                            if len(model_blobs) >= MAX_MODELS:
                                raise RuntimeError('Transmission total model bound')
                            encoded = encode_fgs(mesh); total += len(encoded)
                            if total > MAX_TOTAL_BYTES:
                                raise RuntimeError('Transmission total output bound')
                            auxkey = hashlib.sha256(('FGS3-point-transmission-v1:'+path).encode()).digest()
                            model_blobs[auxkey.hex()] = encoded; mesh['key'] = auxkey
                            counts['models'] += 1
                    except (ValueError,IndexError,FileNotFoundError,struct.error) as exc:
                        meshes[path] = None; skipped[path] = str(exc)
                mesh = meshes[path]
                if mesh is None:
                    continue
                lo = [min(v[k] for v in mesh['vertices']) for k in range(3)]
                hi = [max(v[k] for v in mesh['vertices']) for k in range(3)]
                matrix = [entry[9+i*3:12+i*3] for i in range(3)]; translation = entry[18:21]
                if not all(math.isfinite(v) for v in entry[3:]):
                    raise ValueError('Nonfinite source transmission placement')
                determinant = sum(matrix[0][k]*(matrix[1][(k+1)%3]*matrix[2][(k+2)%3]-matrix[1][(k+2)%3]*matrix[2][(k+1)%3]) for k in range(3))
                if abs(determinant) < 1e-12:
                    raise ValueError('Singular source transmission placement')
                corners = [tuple(a+b for a,b in zip(matvec(matrix,p),translation)) for p in itertools.product(*zip(lo,hi))]
                bounds = [fn(p[k] for p in corners) for fn in (min,max) for k in range(3)]
                if not all(math.isfinite(v) and abs(v) <= 1000000 for v in bounds):
                    raise ValueError('Transmission placement bounds outside FGS3 policy')
                records.append((mesh['key'],uid,category,*bounds,*entry[9:])); counts['placements'] += 1
            if records:
                data = struct.pack('<4sII',b'FGS3',3,len(records))+b''.join(INSTANCE.pack(*r) for r in records)
                total += len(data)
                if total > MAX_TOTAL_BYTES:
                    raise ValueError('Transmission total output bound')
                descriptors[mapname+'/'+tile.name] = data
    # Nothing is written until complete bounded assembly succeeds. In particular
    # an unsupported-only region leaves no empty point-transmission directory.
    for key,blob in model_blobs.items():
        path = destination/'models'/(key+'.fgs'); path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(blob)
    for name,blob in descriptors.items():
        path = destination/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(blob)
    report = {'schema':1, 'format':'Unchanged FGS2/v2 + FGS3/v3; point-shadow-only sibling cache',
              'model_key_namespace':'FGS3-point-transmission-v1:', 'counts':{**dict(counts),'bytes':total,'tiles':len(descriptors)},
              'models':{p:{k:v for k,v in m.items() if k not in ('vertices','triangles','materials','key')} |
                           {'key':m['key'].hex(),'vertices':len(m['vertices']),'triangles':len(m['triangles'])}
                        for p,m in meshes.items() if m},
              'skipped':skipped, 'source_tiles':inputs, 'archive_fingerprint':assets.fingerprint(),
              'limits':{'model_vertices':MAX_VERTICES,'model_triangles':MAX_TRIANGLES,'model_bytes':MAX_MODEL_BYTES,
                        'total_models':MAX_MODELS,'total_bytes':MAX_TOTAL_BYTES},
              'limitations':['Binary 8x8 ordered coverage approximates neutral partial transmission, not coloured absorption or layered transmittance.',
                             'Only proven static literal depth-writing blend2 surfaces absent from native caster triangles are exported; clamp-axis UVs must stay inside [0,1].',
                             'FGS2 wrap addressing is retained; source clamp-border filtering within half a mip texel is not represented exactly.',
                             'Additive/Mod2X/unlit/unsupported animation excluded. Native GI/sun cache files are unchanged.',
                             'The point-shadow worker must route this sibling cache through the existing geometry budget; no runtime support is installed here.']}
    lightsOutput.mkdir(parents=True,exist_ok=True)
    (lightsOutput/'point-transmission-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0]); client_archives.add_arguments(parser)
    parser.add_argument('--cache',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--maps',nargs='+',required=True); args = parser.parse_args(); assets = Assets.from_args(args)
    try:
        result = build(args.cache,args.output,args.maps,assets)
    finally:
        assets.close()
    print(json.dumps(result['counts']))
