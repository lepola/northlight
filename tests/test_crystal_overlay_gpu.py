#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client manual
"""Check crystal reflection depth across camera rotations on a hidden D3D9 device."""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path[:0] = [str(fp.RENDERER), str(fp.REPO/'scripts/shaders')]
from world_scene_builder import Assets, array, decode_blp, string
from extract_signatures import variants
from shader_compiler import Compiler

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', required=True)
parser.add_argument('--baseline-headers', type=Path, help='optional saved production headers for a negative run')
args = parser.parse_args()
out = fp.output_dir()
assets = Assets()
try:
    path = r'world\expansion02\doodads\stormpeaks\iceshards\frostgianticeshard04.m2'
    model = assets.read(path)
    skin = assets.read(path[:-3]+'00.skin')
    count, offset = struct.unpack_from('<II', model, 60)
    vertices = [model[offset+i*48:offset+(i+1)*48] for i in range(count)]
    count, offset = struct.unpack_from('<II', skin, 4)
    lookup = [i[0] for i in array(skin, offset, count, '<H')]
    count, offset = struct.unpack_from('<II', skin, 12)
    indices = [lookup[i[0]] for i in array(skin, offset, count, '<H')]
    (out/'crystal.mesh').write_bytes(struct.pack('<2I', len(vertices), len(indices))+
                                   b''.join(vertices)+struct.pack('<'+'I'*len(indices), *indices))
    count, offset = struct.unpack_from('<II', model, 112)
    assert array(model, offset, count, '<2H') == [(1, 0), (16, 2), (16, 2)]
    count, offset = struct.unpack_from('<II', model, 80)
    texture = array(model, offset, count, '<4I')[0]
    width, height, rgba = decode_blp(assets.read(string(model, texture[3])))
    bgra = bytearray(rgba)
    for i in range(0, len(bgra), 4):
        bgra[i], bgra[i+2] = bgra[i+2], bgra[i]
    (out/'crystal.rgba').write_bytes(struct.pack('<2I', width, height)+bgra)
    for sm in (2, 3):
        for family in ('diffuse_t1', 'diffuse_env'):
            codes = dict((i, b) for i, _, b in variants(assets.read(f'shaders\\vertex\\vs_{sm}_0\\{family}.bls')))
            for variant in (10, 11):
                (out/f'{family}-{sm}-{variant}.bin').write_bytes(codes[variant])
finally:
    assets.close()
source = out/'crystal.hlsl'
source.write_text('float4 CrystalPS(float2 uv:TEXCOORD0):COLOR0{return float4(uv,.5,1);}\n')
Compiler(allow_compatible_binary=True).compile(source, 'CrystalPS', 'ps_3_0', out/'CrystalPS.bin')
exe = out/'crystal-overlay.exe'
extra = ['-I'+str(args.baseline_headers.resolve())] if args.baseline_headers else []
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *extra, *fp.test_include_flags(), str(fp.TESTS/'crystal_overlay_gpu.cpp'),
                '-luser32', '-luuid', '-o', str(exe)], env=fp.zig_env(), check=True)
run = subprocess.run([str(exe), str(Path(args.backend).resolve()), str(out)+'/'],
                     cwd=out, capture_output=True, text=True, timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr)
assert run.returncode == 0, run.stdout+run.stderr
assert 'CRYSTAL_OVERLAY_PASS' in run.stdout
(out/'validation.json').write_text(json.dumps({'backend': args.backend, 'model': path,
                                             'output': run.stdout}, indent=2)+'\n')
print(run.stdout, end='')
