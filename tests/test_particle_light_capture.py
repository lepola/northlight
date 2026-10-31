#!/usr/bin/env python3
# northlight-test: requires=zig,client,stormlib
"""Native shader capture with Cold Blood and display-colored Swamp Gas uploads; no device."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

if os.name != 'nt':
    print('SKIP: native Windows SDK integration runtime required')
    raise SystemExit(0)

fp.use_source_modules()
from world_scene_builder import Assets, array, string, unpack, decode_blp
from extract_signatures import variants
from build_equipment_emitters import lifetime_track
from build_mount_emitters import dbc
from attached_emitter_sources import write_stage


def swamp_gas_fixture(assets):
    """Actual display palettes and flipbook frames; no universal model tint."""
    model = assets.read(r'creature\cloud\cloudswampgas.m2')
    models, names = dbc(assets, 'creaturemodeldata', 28)
    model_ids = {r[0] for r in models if string(names, r[2]) == r'creature\cloud\cloudswampgas.mdx'}
    displays, _ = dbc(assets, 'creaturedisplayinfo', 16)
    palettes, _ = dbc(assets, 'particlecolor', 10)
    by_id = {r[0]: r for r in palettes}
    selected = sorted({r[13] for r in displays if r[1] in model_ids and r[13]})
    assert model_ids == {2307} and 286 in selected
    assert {r[0] for r in displays if r[1] in model_ids and r[13] == 286} == {18075, 20373}
    path = r'creature\cloud\cloudswampgas.m2'
    with tempfile.TemporaryDirectory(prefix='swamp-gas-native-', dir=fp.output_dir()) as folder:
        report = write_stage(assets, Path(folder), {path: {'replacement_colors': selected}}, {}, 'cloud', 'No cloud source')
        entry = report['models'][path]
        assert [s['particles'] for s in entry['emitters']] == [[1]]
        assert set(entry['rejected_regions']['particles']) == {0, 2, 3}
        assert report['counts']['mpq_byte_roundtrips'] == 1
    textures = array(model, *reversed(unpack('<II', model, 80)), '<4I')
    count, offset = unpack('<II', model, 296)
    stride = 492 if unpack('<I', model, 16)[0] & 0x200 else 476
    images, image_ids, cases = [], {}, []
    for particle in range(count):
        p = offset + particle*stride
        blend, _, replacement = unpack('<BBh', model, p+40)
        if replacement not in (11, 12, 13):
            continue
        assert blend == 4
        texture = textures[unpack('<H', model, p+22)[0]]
        assert texture[0] == 0
        name = string(model, texture[3])
        if name not in image_ids:
            image_ids[name] = len(images)
            blob = assets.read(name)
            width, height = unpack('<II', blob, 12)
            offsets, sizes = unpack('<16I', blob, 20), unpack('<16I', blob, 84)
            encoding, depth, alpha, _ = unpack('<4B', blob, 8)
            assert encoding == 2
            level = next(i for i in range(16) if offsets[i] and sizes[i] and max(width >> i, height >> i) <= 32)
            w, h = max(1, width >> level), max(1, height >> level)
            block = 8 if depth in (0, 1) else 16
            d3d = 0x31545844 if block == 8 else 0x33545844 if alpha == 1 else 0x35545844
            raw = blob[offsets[level]:offsets[level]+sizes[level]]
            dw, dh, rgba = decode_blp(blob, 32)
            assert (dw, dh) == (w, h)
            images.append((struct.pack('<5I', w, h, d3d, ((w+3)//4)*block, len(raw))+raw, w, h, rgba))
        image_id = image_ids[name]
        _, w, h, rgba = images[image_id]
        rows, cols = unpack('<2H', model, p+48)
        alphas = lifetime_track(model, p+276, '<h')
        assert len(alphas) == 3
        for palette in selected:
            display_alpha = max(r[5] for r in displays if r[1] in model_ids and r[13] == palette)/255
            for key, (_, (opacity,)) in enumerate(alphas):
                # ParticleColor stores Start[3], Mid[3], End[3]; M2 11..13 selects a column.
                colour = by_id[palette][1+key*3+replacement-11]
                tint = tuple(((colour >> shift) & 255)/255 for shift in (16, 8, 0)) + (opacity/32767.*display_alpha,)
                for cell in range(rows*cols):
                    uv = ((cell % cols+.5)/cols, (cell//cols+.5)/rows)
                    at = (min(h-1, int(uv[1]*h))*w+min(w-1, int(uv[0]*w)))*4
                    texel = tuple(v/255 for v in rgba[at:at+4])
                    cases.append(struct.pack('<4I10f', image_id, palette, particle, cell, *tint, *uv, *texel))
    assert len(images) == 2 and len(cases) == len(selected)*3*(1+64+64)
    print(f'Swamp Gas: model 2307, cyan displays 18075/20373 palette 286; {len(cases)} palette/lifetime/atlas cases', flush=True)
    return (struct.pack('<I', len(images)) + b''.join(v[0] for v in images) +
            struct.pack('<I', len(cases)) + b''.join(cases))


def native_fixture():
    assets = Assets(archives='stock')
    try:
        data = bytearray()
        for path, index in ((r'shaders\vertex\vs_3_0\CDiffuse_T1.bls', 0),
                            (r'shaders\vertex\vs_3_0\Color_T1.bls', 1),
                            (r'shaders\vertex\vs_3_0\Diffuse_T1.bls', 0),
                            (r'shaders\pixel\ps_3_0\Combiners_Mod.bls', 0)):
            code = list(variants(assets.read(path)))[index][2]
            data.extend(struct.pack('<I', len(code) // 4))
            data.extend(code)
        blob = assets.read(r'particles\snowflake.blp')
        assert blob[:4] == b'BLP2'
        encoding, depth, alpha, _ = unpack('<4B', blob, 8)
        assert encoding == 2
        width, height = unpack('<II', blob, 12)
        offsets, sizes = unpack('<16I', blob, 20), unpack('<16I', blob, 84)
        level = next(i for i in range(16) if offsets[i] and sizes[i] and max(width >> i, height >> i) <= 32)  # the cache keeps mips up to 32 texels
        width, height = max(1, width >> level), max(1, height >> level)
        block = 8 if depth in (0, 1) else 16
        d3d = 0x31545844 if block == 8 else 0x33545844 if alpha == 1 else 0x35545844
        raw = blob[offsets[level]:offsets[level] + sizes[level]]
        data.extend(struct.pack('<5I', width, height, d3d, ((width + 3) // 4) * block, len(raw)))
        data.extend(raw)
        model = assets.read(r'spells\ice_precast_med_hand.m2')
        assert model[:4] == b'MD20' and unpack('<I', model, 4)[0] == 264
        count, offset = unpack('<II', model, 80)
        textures = [string(model, name) for _, _, _, name in array(model, offset, count, '<4I')]
        count, offset = unpack('<II', model, 296)
        stride = 492 if unpack('<I', model, 16)[0] & 0x200 else 476
        source = next(offset + i * stride for i in range(count)
                      if textures[unpack('<H', model, offset + i * stride + 22)[0]].lower() == r'particles\snowflake.blp')
        assert model[source + 40] == 4  # Native SrcAlpha / One.
        colors = lifetime_track(model, source + 260, '<3f')
        alpha = lifetime_track(model, source + 276, '<h')
        assert colors[0][0] == alpha[0][0] == 0
        data.extend(struct.pack('<4f', *(v / 255 for v in colors[0][1]), alpha[0][1][0] / 32767))
        data.extend(swamp_gas_fixture(assets))
        return data
    finally:
        assets.close()


with tempfile.TemporaryDirectory(prefix='northlight-particle-capture-') as temporary:
    root = Path(temporary)
    fixture = root / 'native.bin'
    fixture.write_bytes(native_fixture())
    for architecture in ('x86', 'x86_64'):
        for optimization in ('-O2', '-O0'):
            binary = root / (architecture + optimization + '.exe')
            subprocess.run([str(fp.zig()), 'c++', '-target', architecture + '-windows-gnu', optimization,
                            '-UNDEBUG', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
                            str(Path(__file__).with_suffix('.cpp')), '-o', str(binary)], env=fp.zig_env(), check=True)
            print(architecture, optimization, flush=True)
            subprocess.run([str(binary), str(fixture)], check=True)
