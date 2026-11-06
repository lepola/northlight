"""Compile Northlight material roles from the active, read-only client archive view.

The catalog contains draw-material identities, never map/area/skybox IDs. Every
LightSkybox entry and every available skin is audited; new texture names and
ambiguous identities fail the build instead of silently changing composition.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets, array, blp_mips, decode_blp
from build_outdoor_single_sun import texture_names

ROLES = ROOT/'src/sky/sky_material_roles.json'
HEADER = ROOT/'src/generated/sky_material_catalog.h'
MANIFEST = ROOT/'src/sky/sky-material-build.json'


def name(value):
    value = value.lower().replace('/', '\\')
    return value.split('.blp')[0]+'.blp' if '.blp' in value else value.strip()


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte)*1099511628211) & 0xffffffffffffffff
    return value


def role(materials, texture, primitives):
    item = materials[texture.rsplit('\\', 1)[-1]]
    if isinstance(item, dict):
        return 'background' if primitives in item['background_primitives'] else 'foreground'
    if item not in ('background', 'foreground'):
        raise ValueError(f'Invalid sky material role: {texture}: {item}')
    return item


def compile_catalog(assets):
    materials = json.loads(ROLES.read_text())['materials']
    inputs = {}
    def read(path):
        blob = assets.read(path)
        inputs[path.lower()] = hashlib.sha256(blob).hexdigest()
        return blob
    dbc = read('DBFilesClient\\LightSkybox.dbc')
    assert dbc[:4] == b'WDBC'
    rows, fields, stride, strings_size = struct.unpack_from('<4I', dbc, 4)
    assert fields >= 3 and stride >= 12
    strings = dbc[20+rows*stride:20+rows*stride+strings_size]
    models = set()
    for i in range(rows):
        offset = struct.unpack_from('<I', dbc, 20+i*stride+4)[0]
        model = strings[offset:].split(b'\0', 1)[0].decode().lower()
        if model:
            models.add(model[:-4]+'.m2')
    records, textures, batches, unavailable_skins, unavailable_textures = {}, {}, 0, [], set()
    for path in sorted(models):
        model = read(path)
        names, lookup = texture_names(model)
        for skin_id in range(struct.unpack_from('<I', model, 68)[0]):
            skin_path = path[:-3]+f'{skin_id:02d}.skin'
            try:
                skin = read(skin_path)
            except FileNotFoundError:
                if skin_id == 0:
                    raise
                unavailable_skins.append(skin_path)
                continue
            assert skin[:4] == b'SKIN'
            n, offset = struct.unpack_from('<II', skin, 28)
            subsets = array(skin, offset, n, '<10H7f')
            n, offset = struct.unpack_from('<II', skin, 36)
            for batch in array(skin, offset, n, '<2B11H'):
                primitives = subsets[batch[3]][5]//3
                assert batch[8] and primitives
                texture = name(names[lookup[batch[9]]])
                material_role = role(materials, texture, primitives)
                batches += 1
                if texture not in textures:
                    try:
                        blob = read(texture)
                    except FileNotFoundError:
                        unavailable_textures.add(texture)
                        textures[texture] = []
                        continue
                    encoding, alpha_depth, alpha_encoding = struct.unpack_from('<3B', blob, 8)
                    mips = []
                    for level, w, h, pos, size in blp_mips(blob):
                        if max(w, h) > 128:
                            continue
                        if encoding == 2:
                            fmt = 0x31545844 if alpha_depth <= 1 else 0x33545844 if alpha_encoding == 1 else 0x35545844
                            data = blob[pos:pos+size]
                        elif encoding == 3:
                            fmt, data = 21, blob[pos:pos+size]
                        else:
                            rw, rh, rgba = decode_blp(blob, max(w, h))
                            assert (rw, rh) == (w, h)
                            data = bytearray(rgba)
                            data[0::4], data[2::4] = rgba[2::4], rgba[0::4]
                            fmt = 21
                        mips.append((fnv(data), fmt, w, h))
                    assert mips, texture
                    textures[texture] = mips
                for mip in textures[texture]:
                    key = (*mip, primitives)
                    previous = records.get(key)
                    if previous and previous[0] != material_role:
                        raise ValueError(f'Ambiguous material identity: {previous} / {(material_role,texture,primitives)}')
                    records[key] = material_role, texture
    lines = ['// Generated by scripts/build_sky_material_catalog.py; no zone IDs.', '#pragma once', '#include "sky_composition.h"',
             '#include <cstddef>', 'namespace NorthlightSkyComposition {',
             'struct Material {std::uint64_t hash;unsigned format,width,height,primitives;Role role;};',
             'inline constexpr Material Materials[]={']
    for (digest, fmt, w, h, primitives), (material_role, texture) in sorted(records.items()):
        lines.append(f'    {{0x{digest:016x}ull,{fmt}u,{w},{h},{primitives},Role::{material_role.title()}}},')
    lines += ['};', 'inline Role materialRole(std::uint64_t hash,unsigned format,unsigned width,unsigned height,unsigned primitives){',
              '    std::size_t lo=0,hi=sizeof(Materials)/sizeof(Materials[0]);',
              '    while(lo<hi){const auto mid=(lo+hi)/2;if(Materials[mid].hash<hash)lo=mid+1;else hi=mid;}',
              '    for(;lo<sizeof(Materials)/sizeof(Materials[0])&&Materials[lo].hash==hash;++lo){const auto& m=Materials[lo];',
              '        if(m.format==format&&m.width==width&&m.height==height&&m.primitives==primitives)return m.role;}',
              '    return Role::Unknown;', '}', '}']
    header = ('\n'.join(lines)+'\n').encode()
    manifest = dict(version=1, models=len(models), batches=batches, textures=len(textures), identities=len(records),
                    unavailable_skins=unavailable_skins, unavailable_textures=sorted(unavailable_textures), roles_sha256=hashlib.sha256(ROLES.read_bytes()).hexdigest(),
                    generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    header_sha256=hashlib.sha256(header).hexdigest(), inputs=dict(sorted(inputs.items())))
    return header, (json.dumps(manifest, indent=2)+'\n').encode(), manifest


def check_generated():
    manifest = json.loads(MANIFEST.read_text())
    for path, field in ((ROLES, 'roles_sha256'), (Path(__file__), 'generator_sha256'), (HEADER, 'header_sha256')):
        if hashlib.sha256(path.read_bytes()).hexdigest() != manifest[field]:
            raise ValueError(f'Stale sky material catalog: {path}; run scripts/build_sky_material_catalog.py')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    assets = Assets()
    try:
        header, report, manifest = compile_catalog(assets)
    finally:
        assets.close()
    if args.check:
        assert HEADER.read_bytes() == header and MANIFEST.read_bytes() == report, 'Sky catalog differs from active client inputs'
    else:
        HEADER.write_bytes(header)
        MANIFEST.write_bytes(report)
    print('Sky catalog:', {k:manifest[k] for k in ('models', 'batches', 'textures', 'identities')})


if __name__ == '__main__':
    main()
