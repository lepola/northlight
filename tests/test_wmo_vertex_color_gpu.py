#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Authentic WMO shader/attribute regression for admission-independent native lighting."""
import argparse
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets, chunks, wmo_group
from extract_signatures import variants, fnv1a64
from validate_native_scene_trace import processes

p = argparse.ArgumentParser(__doc__)
p.add_argument('--compile-only', action='store_true')
p.add_argument('--backend', type=Path, action='append')
a = p.parse_args()
out = fp.output_dir()/'wmo-vertex-color-gpu'
out.mkdir(parents=True, exist_ok=True)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = [fp.src(n) for n in ('wmo_vertex_color.h', 'displacement_geometry.h', 'displacement_shader_patch.h',
                              'auto_displacement_renderer.h', 'auto_bump_renderer.h', 'actor_deformation.h')]
inputs += [Path(__file__), fp.TESTS/'wmo_vertex_color_gpu.cpp']
report = dict(game_launched=False, source_bytes_committed=False, timings_claimed=False,
              input_sha256={str(path): sha(path) for path in inputs}, shaders=[], groups=[], backends=[])
report['pass'] = False
report['references'] = {'color_and_alpha': 'original native coarse shader, same UNORM target',
                        'uv_and_fog': 'unpreserved native displacement shader on identical subdivided geometry',
                        'maximum_byte_error': 1}
registered = {int(h, 16) for h in re.findall(r'UINT64_C\(0x([a-f0-9]+)\),2', fp.src('auto_bump_identities.h').read_text())}
seen = set()
with closing(Assets(archives='stock')) as assets:
    for name in assets.providers:
        if not re.fullmatch(r'shaders/vertex/vs_[23]_0/mapobj[^/]+\.bls', name.lower().replace('\\', '/')):
            continue
        for index, flags, code in variants(assets.read(name)):
            identity = fnv1a64(code)
            if identity not in registered or identity in seen:
                continue
            seen.add(identity)
            file = out/f'vs-{identity:016x}.bin'
            file.write_bytes(code)
            report['shaders'].append(dict(file=str(file), path=name, variant=index, sha256=sha(file), identity=f'{identity:016x}'))
assert len(seen) == 72, 'Explicit stock coverage must be reviewed if the client shader catalog changes'
(out/'catalog.txt').write_text(''.join(row['file']+'\n' for row in report['shaders']))

# Exact original native WMO attributes on both sides of the captured wall and
# ground seams. Rigidly reorient each pair for the independent offscreen oracle;
# retain original normals, vertex colours and UVs (no FGS reconstruction).
groups = {}
with closing(Assets()) as assets:
    for group in (280, 284):
        name = f'world/wmo/azeroth/buildings/stormwind/stormwind_{group:03}.wmo'
        blob = assets.read(name)
        top, _ = wmo_group(blob)
        parts = list(chunks(top['MOGP'], 68))
        first = lambda tag: next(value for key, value in parts if key == tag)
        groups[group] = dict(pos=np.frombuffer(first('MOVT'), '<f4').reshape(-1, 3),
                             normal=np.frombuffer(first('MONR'), '<f4').reshape(-1, 3),
                             uv=np.frombuffer(first('MOTV'), '<f4').reshape(-1, 2),
                             color=np.frombuffer(first('MOCV'), '<u4'),
                             indices=np.frombuffer(first('MOVI'), '<u2'))
        report['groups'].append(dict(group=group, path=name, sha256=hashlib.sha256(blob).hexdigest()))
payload = bytearray(struct.pack('<II', 0x314d5746, 2))
for pair in (((284, 957), (280, 7726)), ((284, 14384), (280, 17758))):
    positions, normals, colors, uvs = [], [], [], []
    for group, triangle in pair:
        g = groups[group]
        ids = g['indices'][triangle*3:triangle*3+3]
        positions.extend(g['pos'][ids]); normals.extend(g['normal'][ids]); colors.extend(g['color'][ids]); uvs.extend(g['uv'][ids])
    positions = np.asarray(positions, dtype='f8'); normals = np.asarray(normals, dtype='f8')
    centre = positions.mean(axis=0)
    n = normals.mean(axis=0); n /= np.linalg.norm(n)
    u = positions[1]-positions[0]; u -= n*np.dot(u, n); u /= np.linalg.norm(u)
    v = np.cross(n, u)
    rotation = np.array([u, v, -n])
    oriented = (positions-centre)@rotation.T + np.array([0, 0, 8])
    oriented_normals = normals@rotation.T
    for pos, normal, color, uv in zip(oriented, oriented_normals, colors, uvs):
        payload += struct.pack('<6fI2f', *pos, *normal, int(color), *uv)
(out/'seams.fwm').write_bytes(payload)
report['seam_sha256'] = sha(out/'seams.fwm')

exe = out/'wmo-color.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           '-Wno-microsoft-exception-spec',
           *fp.test_include_flags(), fp.TESTS/'wmo_vertex_color_gpu.cpp', '-luser32', '-o', exe]
with (out/'compile.log').open('w') as log:
    subprocess.run(list(map(str, command)), env=fp.zig_env(), check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
if a.compile_only:
    print('WMO_VERTEX_COLOR_GPU_COMPILED', exe)
    sys.exit(0)
backends = a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                        fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                        fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
try:
    for i, backend in enumerate(backends):
        processes(False)
        result = subprocess.run([str(exe), str(backend), str(out/'catalog.txt'), str(out/'seams.fwm')], cwd=out,
                                env={**fp.zig_env(), 'DXVK_LOG_PATH': str(out), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
                                capture_output=True, text=True, timeout=180)
        (out/f'gpu-{i}.log').write_text(result.stdout+result.stderr)
        print(backend, result.returncode, (result.stdout+result.stderr)[-1800:], flush=True)
        assert result.returncode == 0 and 'WMO_VERTEX_COLOR_GPU_PASS' in result.stdout and 'WMO_PRODUCTION_INTEGRATION_PASS cases=18' in result.stdout
        metrics = re.search(r'comparisons=(\d+) nativeColorAlphaUVFog=1.*maxByteError=(\d+) baselineFailureControls=(\d+)', result.stdout)
        assert metrics and int(metrics[1]) == 2610 and int(metrics[2]) <= 1 and int(metrics[3]) == 72
        report['backends'].append(dict(path=str(backend), sha256=sha(backend), comparisons=2610,
                                       production_integration_cases=18, baseline_failure_controls=72))
    report['pass'] = True
finally:
    report['inputs_unchanged'] = all(sha(Path(path)) == digest for path, digest in report['input_sha256'].items())
    report['pass'] &= report['inputs_unchanged']
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
assert report['pass']
