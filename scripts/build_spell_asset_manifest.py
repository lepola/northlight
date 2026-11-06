#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only spell/hand asset provenance; optional local binaries, never an MPQ patch."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
from world_scene_builder import Assets, array, blp_mips, decode_blp, string, unpack
from build_spell_emitters import dbc_table, spell_sources
from build_equipment_emitters import lifetime_track
from world_lights_builder import constant_track

PIN = 'b1c71b8bd108823bca952f16c6f83a7ad33b9648'
PRIMARY = 'https://github.com/SatyPardus/wowdecomp/blob/' + PIN + '/'
VISUAL_KITS = {1: 'precast', 2: 'cast', 3: 'impact', 4: 'state', 5: 'stateDone',
               6: 'channel', 14: 'casterImpact', 15: 'targetImpact',
               22: 'missileTargeting', 23: 'instantArea', 24: 'impactArea', 25: 'persistentArea'}
KIT_EFFECTS = ('head', 'chest', 'base', 'leftHand', 'rightHand', 'breath',
               'leftWeapon', 'rightWeapon', 'special0', 'special1', 'special2', 'world')


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def write_json(path, value):
    path.write_bytes((json.dumps(value, indent=2) + '\n').encode('utf-8'))


def fnv1a64(blob):
    value = 14695981039346656037
    for byte in blob:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return '%016x' % value


def provenance(assets, path, blob):
    return {'path': path, 'archive': assets.origin(path), 'bytes': len(blob), 'sha256': sha(blob)}


def dbc(assets, name, fields):
    path = 'dbfilesclient\\' + name + '.dbc'
    blob, rows, strings = dbc_table(assets, path)
    if unpack('<I', blob, 8)[0] != fields:
        raise ValueError('Unsupported DBC schema: ' + path)
    return {r[0]: r for r in rows}, strings, provenance(assets, path, blob)


def resolve_spell(assets, spell_id):
    spells, names, spell_meta = dbc(assets, 'spell', 234)
    visuals, _, visual_meta = dbc(assets, 'spellvisual', 32)
    kits, _, kit_meta = dbc(assets, 'spellvisualkit', 38)
    effects, paths, effect_meta = dbc(assets, 'spellvisualeffectname', 7)
    attachments, _, attach_meta = dbc(assets, 'spellvisualkitmodelattach', 10)
    spell = spells[spell_id]
    graph = []
    for visual in (visuals[v] for v in spell[131:133] if v):
        for column, stage in VISUAL_KITS.items():
            if not visual[column]:
                continue
            kit = kits[visual[column]]
            uses = [(effects[e], {'slot': KIT_EFFECTS[i], 'attachment_id': 22 if i == 4 else None})
                    for i, e in enumerate(kit[3:15]) if e]
            for a in attachments.values():
                if a[1] == kit[0]:
                    uses.append((effects[a[2]], {'slot': 'modelAttach', 'attachment_id': a[3], 'row_id': a[0]}))
            for effect, attachment in uses:
                path = string(paths, effect[2])
                if path.endswith(('.mdx', '.mdl')):
                    path = path[:-4] + '.m2'
                graph.append({'visual_id': visual[0], 'visual_column': column, 'stage': stage,
                              'kit_id': kit[0], 'effect_id': effect[0], 'effect_label': string(paths, effect[1]),
                              **attachment, 'model': path})
    return {'id': spell_id, 'name': string(names, spell[136]), 'visual_ids': list(spell[131:133]),
            'effect_ids': list(spell[71:74]), 'effect_aura_ids': list(spell[95:98]),
            'trigger_spell_ids': list(spell[116:119]), 'graph': graph}, [spell_meta, visual_meta, kit_meta, effect_meta, attach_meta]


def rgba_moments(rgba):
    if not rgba or len(rgba) % 4:
        raise ValueError('Invalid RGBA pixels')
    pixels = len(rgba) // 4
    rgb = [sum(rgba[k::4]) / (255 * pixels) for k in range(3)]
    rgb_alpha = [sum(rgba[p+k] * rgba[p+3] for p in range(0, len(rgba), 4)) / (255 * 255 * pixels) for k in range(3)]
    return {'rgb_mean': rgb, 'rgb_alpha_mean': rgb_alpha, 'alpha_mean': sum(rgba[3::4]) / (255 * pixels)}


def runtime_reference(raw, width, height, pitch, fmt, decoder, output=None):
    with tempfile.TemporaryDirectory(prefix='northlight-mip-') as temp:
        source, target = Path(temp) / 'mip.bin', Path(temp) / 'rgba.bin'
        source.write_bytes(raw)
        subprocess.run([str(decoder), str(source), str(target), str(width), str(height), str(pitch), fmt], check=True, capture_output=True)
        rgba = target.read_bytes()
    if len(rgba) != width * height * 4:
        raise ValueError('Native decoder returned unexpected RGBA extent')
    if output:
        output.write_bytes(rgba)
    return {'decoder': 'NorthlightActorTexture::decode', 'rgba_sha256': sha(rgba), **rgba_moments(rgba)}


def texture_manifest(assets, path, local=None, decoder=None):
    blob = assets.read(path)
    if blob[:4] != b'BLP2':
        raise ValueError('Not BLP2: ' + path)
    encoding, depth, alpha, has_mips = unpack('<4B', blob, 8)
    width, height = unpack('<II', blob, 12)
    if encoding != 2:
        raise ValueError('This fixture requires native BC texture payloads')
    bc = 'DXT1' if depth in (0, 1) else 'DXT3' if alpha == 1 else 'DXT5'
    offsets, sizes = unpack('<16I', blob, 20), unpack('<16I', blob, 84)
    mips = []
    for level, w, h, offset, size in blp_mips(blob):
        pitch = ((w + 3) // 4) * (8 if bc == 'DXT1' else 16)
        raw = blob[offset:offset+size]
        if len(raw) != size or size != pitch * ((h + 3) // 4):
            raise ValueError('Unexpected native BC mip extent')
        # An in-memory decode view selects this original mip; no client bytes are written.
        view = bytearray(blob)
        struct.pack_into('<II', view, 12, w, h)
        struct.pack_into('<16I', view, 20, offset, *([0] * 15))
        struct.pack_into('<16I', view, 84, size, *([0] * 15))
        _, _, rgba = decode_blp(bytes(view), max(w, h))
        item = {'level': level, 'width': w, 'height': h, 'pitch': pitch, 'bytes': size,
                'raw_sha256': sha(raw), 'raw_fnv1a64': fnv1a64(raw), 'rgba_sha256': sha(rgba), **rgba_moments(rgba)}
        mips.append(item)
        if local:
            stem = local / (path.replace('\\', '_') + '.mip%d' % level)
            stem.with_suffix(stem.suffix + '.bin').write_bytes(raw)
            stem.with_suffix(stem.suffix + '.rgba').write_bytes(rgba)
    selected = next((m for m in mips if max(m['width'], m['height']) <= 64), None)
    reference = None
    if decoder and selected:
        level = selected['level']
        reference = {'level': level, **runtime_reference(blob[offsets[level]:offsets[level]+selected['bytes']], selected['width'],
                                                        selected['height'], selected['pitch'], bc, decoder,
                                                        local / (path.replace('\\', '_') + '.mip%d.runtime.rgba' % level) if local else None)}
    return {**provenance(assets, path, blob), 'blp_encoding': encoding, 'alpha_depth': depth,
            'alpha_encoding': alpha, 'has_mips': has_mips, 'native_format': bc,
            'd3d_format': int.from_bytes(bc.encode('ascii'), 'little'), 'width': width, 'height': height, 'mips': mips,
            'runtime_reference': reference}


def track(blob, offset, fmt):
    mode, sequence, nt, ot, nk, ok = unpack('<hh4I', blob, offset)
    if nt != nk or nt > 128 or mode not in (0, 1):
        raise ValueError('Unsupported fixture animation track')
    curves = []
    for (tc, to), (kc, ko) in zip(array(blob, ot, nt, '<II'), array(blob, ok, nk, '<II')):
        if tc != kc or tc > 4096:
            raise ValueError('Invalid fixture track keys')
        curves.append({'times': [v[0] for v in array(blob, to, tc, '<I')],
                       'values': array(blob, ko, kc, fmt)})
    return {'interpolation': mode, 'sequence': sequence, 'curves': curves}


def model_manifest(assets, path, stock, local=None):
    blob, original = assets.read(path), stock.read(path)
    if blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Not v264 M2')
    count, offset = unpack('<II', blob, 44)
    bones = []
    for i in range(count):
        p = offset + i * 88
        bones.append({'index': i, 'key_bone_id': unpack('<i', blob, p)[0], 'flags': unpack('<I', blob, p+4)[0],
                      'parent': unpack('<h', blob, p+8)[0], 'pivot': unpack('<3f', blob, p+76),
                      'translation': track(blob, p+16, '<3f'), 'rotation_compressed': track(blob, p+36, '<4h'),
                      'scale': track(blob, p+56, '<3f')})
    n, off = unpack('<II', blob, 80)
    textures = [{'type': t, 'flags': flags, 'path': string(blob, name)} for t, flags, _, name in array(blob, off, n, '<4I')]
    # Existing lights are only ignored in a disposable qualification copy, never on disk.
    probe = bytearray(blob)
    struct.pack_into('<II', probe, 264, 0, 0)
    sources, rejected = spell_sources(assets, bytes(probe), max_sources=None)
    by_particle = {i: s for s in sources for i in s['particles']}
    n, off = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    particles = []
    for i in range(n):
        p = off + i * stride
        bone, texture = unpack('<hH', blob, p+20)
        s = by_particle.get(i)
        particles.append({'index': i, 'flags': unpack('<I', blob, p+4)[0], 'bone': bone,
                          'position': unpack('<3f', blob, p+8), 'texture_index': texture, 'texture': textures[texture]['path'],
                          'blend': unpack('<B', blob, p+40)[0], 'emitter_type': unpack('<B', blob, p+41)[0],
                          'atlas_rows_columns': unpack('<2H', blob, p+48),
                          'rate': track(blob, p+176, '<f'), 'enabled': track(blob, p+456, '<B'),
                          'lifespan': track(blob, p+152, '<f'), 'lifespan_variation': unpack('<f', blob, p+172)[0],
                          'rate_variation': unpack('<f', blob, p+196)[0], 'width': track(blob, p+200, '<f'),
                          'length': track(blob, p+220, '<f'), 'rgb_lifetime_0_255': lifetime_track(blob, p+260, '<3f'),
                          'alpha_lifetime_0_32767': lifetime_track(blob, p+276, '<h'), 'scale_lifetime': lifetime_track(blob, p+292, '<2f'),
                          'twinkle': unpack('<4f', blob, p+352), 'qualified': s is not None,
                          'asset_average_diagnostic': {'normalized_linear_rgb': s['rgb'], 'intensity': s['intensity']} if s else None})
    lights = []
    n, off = unpack('<II', blob, 264)
    for i in range(n):
        p = off + i * 156
        lights.append({'type': unpack('<H', blob, p)[0], 'bone': unpack('<H', blob, p+2)[0],
                       'position': unpack('<3f', blob, p+4), 'rgb': constant_track(blob, p+56, '<3f'),
                       'intensity': constant_track(blob, p+76, '<f')[0], 'enabled': constant_track(blob, p+136, '<B')[0]})
    n, off = unpack('<II', blob, 28)
    sequences = [{'id': unpack('<H', blob, p)[0], 'sub_id': unpack('<H', blob, p+2)[0],
                  'duration_ms': unpack('<I', blob, p+4)[0], 'flags': unpack('<I', blob, p+12)[0]}
                 for p in range(off, off+n*64, 64)]
    vertex_count, vertex_offset = unpack('<II', blob, 60)
    preserved = bytearray(blob[:len(original)])
    for start, end in ((20, 28), (264, 272)):
        preserved[start:end] = original[start:end]
    result = {**provenance(assets, path, blob), 'stock': provenance(stock, path, original),
              'stock_prefix_preserved_except_global_sequence_and_light_headers': preserved == original,
              'version': 264, 'vertex_count': vertex_count, 'ribbon_count': unpack('<I', blob, 288)[0],
              'sequences': sequences, 'bones': bones, 'textures': textures, 'particles': particles,
              'native_lights': lights, 'rejected_particles': rejected}
    if local:
        skin_path = path[:-3] + '00.skin'
        skin = assets.read(skin_path)
        if skin[:4] != b'SKIN':
            raise ValueError('Not SKIN')
        n, off = unpack('<II', skin, 4)
        lookup = [v[0] for v in array(skin, off, n, '<H')]
        n, off = unpack('<II', skin, 12)
        triangles = [v[0] for v in array(skin, off, n, '<H')]
        n, off = unpack('<II', skin, 36)
        batches = array(skin, off, n, '<Bb11H')
        n, off = unpack('<II', blob, 112)
        materials = array(blob, off, n, '<HH')
        n, off = unpack('<II', blob, 128)
        texture_lookup = [v[0] for v in array(blob, off, n, '<H')]
        fixture = {'kind': 'original static M2 mesh input; not a captured native particle draw',
                   'model': path, 'sha256': sha(blob), 'skin': provenance(assets, skin_path, skin),
                   'vertex_stride': 48, 'vertex_format': '<3f4B4B3f4f',
                   'vertex_fields': ['positionXYZ', 'boneWeights4', 'boneIndices4', 'normalXYZ', 'uv0', 'uv1'],
                   'vertices': array(blob, vertex_offset, vertex_count, '<3f4B4B3f4f'),
                   'skin_vertex_lookup': lookup, 'skin_triangle_indices': triangles, 'skin_batches': batches,
                   'raw_material_flags_blends': materials, 'texture_lookup': texture_lookup, 'model_metadata': result}
        write_json(local / (Path(path).name + '.fixture.json'), fixture)
        (local / (Path(path).name + '.vertices.bin')).write_bytes(blob[vertex_offset:vertex_offset+48*vertex_count])
    return result


def build(assets, stock, spell_id=14177, local=None, decoder=None):
    if local:
        local.mkdir(parents=True, exist_ok=True)
    spell, tables = resolve_spell(assets, spell_id)
    models = [model_manifest(assets, path, stock, local) for path in sorted({g['model'] for g in spell['graph']})]
    textures = [texture_manifest(assets, path, local, decoder) for path in sorted({t['path'] for m in models for t in m['textures'] if t['type'] == 0})]
    return {'schema': 1, 'read_only': True, 'spell': spell, 'dbc': tables, 'models': models, 'textures': textures,
            'semantics': {'particle_blend_3': 'D3D ONE/ONE', 'particle_blend_4': 'D3D SRCALPHA/ONE',
                          'bone_space': 'Effect M2 local bones; attachment22 is resolved on the parent model by the client.',
                          'diagnostic_colour': 'Actual texture/lifetime RGB diagnostic only; runtime must use submitted particle vertices, shader and blend.',
                          'texture_moments': 'Per-mip BLP reference uses world_scene_builder.decode_blp; optional runtime_reference uses ActorTexture::decode on the most detailed uploaded mip <=64. Both use UNORM moments, no sRGB transform.',
                          'fnv': 'Standard FNV1a64, offset14695981039346656037, prime1099511628211.',
                          'lifecycle': 'Constant rate/enable tracks do not prove the aura is active. CEffect external retirement disables emitters and allows existing particle tails. Publish only current-frame accepted draws; no TTL.'},
            'primary': [PRIMARY + 'WoW/Source/DB/Rec/SpellRec.h', PRIMARY + 'WoW/Source/DB/Rec/SpellVisualRec.h',
                        PRIMARY + 'WoW/Source/DB/Rec/SpellVisualKitRec.h', PRIMARY + 'WoW/Source/DB/Rec/SpellVisualKitModelAttachRec.h',
                        PRIMARY + 'WoW/Source/Object/ObjectClient/Object_C.cpp#L1727',
                        PRIMARY + 'WoW/Source/Object/ObjectClient/Object_C.cpp#L1439',
                        PRIMARY + 'WoW/Source/Object/ObjectClient/Effect_C.cpp#L197',
                        PRIMARY + 'WoW/Source/Object/ObjectClient/Effect_C.cpp#L684']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spell-id', type=int, default=14177)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--local-fixtures', type=Path, help='Optional extracted mip/mesh fixture files; keep outside Git.')
    parser.add_argument('--native-decoder', type=Path, help='Optional executable built from tests/cold_blood_texture_probe.cpp.')
    args = parser.parse_args()
    assets, stock = Assets(), Assets(archives='stock')
    try:
        report = build(assets, stock, args.spell_id, args.local_fixtures, args.native_decoder)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_json(args.output, report)
        print(json.dumps({'spell': report['spell']['id'], 'models': len(report['models']), 'textures': len(report['textures']), 'output': str(args.output)}))
    finally:
        assets.close()
        stock.close()


if __name__ == '__main__':
    main()
