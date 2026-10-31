"""Offline skinned seam/eye GPU regression, optionally using local client M2s."""
from pathlib import Path
import argparse
import json
import os
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root), str(root/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', required=True)
parser.add_argument('--client', action='store_true', help='read Human/Draenei head geosets from local MPQs')
args = parser.parse_args()
if os.name == 'nt':
    running = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq Wow.exe', '/FO', 'CSV'], text=True)
    assert '"Wow.exe"' not in running, 'Close WoW before the offline GPU test.'
out = fp.output_dir()
source = out/'seams.hlsl'
source.write_text('float4 SeamVS(float3 p:POSITION0):POSITION0{return float4(p,1);}\n'
                  'float4 Color:register(c0);float4 SeamPS():COLOR0{return Color;}\n')
compiler = Compiler(allow_compatible_binary=True)
for entry, target in [('SeamVS', 'vs_3_0'), ('SeamPS', 'ps_3_0')]:
    compiler.compile(source, entry, target, out/(entry+'.bin'))

fixtures = []
if args.client:
    sys.path.insert(0, str(root/'renderer'))
    from world_scene_builder import Assets, array
    assets = Assets()
    try:
        for race in ('human', 'draenei'):
            path = f'character\\{race}\\female\\{race}female.m2'
            model = assets.read(path)
            skin = assets.read(path[:-3]+'00.skin')
            assert model[:4] == b'MD20' and skin[:4] == b'SKIN'
            count, offset = struct.unpack_from('<II', model, 60)
            vertices = [model[offset+i*48:offset+(i+1)*48] for i in range(count)]
            count, offset = struct.unpack_from('<II', skin, 4)
            lookup = [v[0] for v in array(skin, offset, count, '<H')]
            count, offset = struct.unpack_from('<II', skin, 12)
            indices = [lookup[v[0]] for v in array(skin, offset, count, '<H')]
            count, offset = struct.unpack_from('<II', skin, 28)
            for index, subset in enumerate(array(skin, offset, count, '<10H7f')):
                if subset[0] != 0:
                    continue
                first = subset[4]+(subset[1] << 16)
                selected = indices[first:first+subset[5]]
                ids = sorted(set(selected))
                if not ids or max(struct.unpack_from('<f', vertices[i], 8)[0] for i in ids) < 1.5:
                    continue
                remap = {v: i for i, v in enumerate(ids)}
                fixture = out/f'{race}-{index}.mesh'
                fixture.write_bytes(struct.pack('<3I', 0x314d3246, len(ids), len(selected)) +
                                    b''.join(vertices[i] for i in ids) +
                                    struct.pack('<'+'I'*len(selected), *(remap[i] for i in selected)))
                fixtures.append(fixture)
    finally:
        assets.close()

exe = out/'m2-seams.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(root/'tests/m2_displacement_seams_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
run = subprocess.run([str(exe), str(Path(args.backend).resolve()), str(out)+'/', *map(str, fixtures)],
                     cwd=out, env=fp.zig_env(), capture_output=True, text=True, timeout=90)
(out/'gpu.log').write_text(run.stdout+run.stderr)
assert run.returncode == 0, run.stdout+run.stderr
assert 'M2_DISPLACEMENT_SEAMS_PASS' in run.stdout
(out/'validation.json').write_text(json.dumps({'backend': args.backend, 'client_meshes': len(fixtures),
                                             'output': run.stdout}, indent=2)+'\n')
print(run.stdout, end='')
