#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Replay the .9 HumanFemale camera fixture with native skinned receiver capture."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_signatures import variants
from world_scene_builder import Assets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--backend', type=Path, required=True)
parser.add_argument('--output', type=Path)
parser.add_argument('--quick', action='store_true')
args = parser.parse_args()
out = (args.output or fp.output_dir()/'model-normals').resolve()
out.mkdir(parents=True, exist_ok=True)
fixture = args.fixture.resolve()
inputs = json.loads((fixture/'inputs.json').read_text())
assert inputs['mesh_sha256'] == hashlib.sha256((fixture/'human-0.mesh').read_bytes()).hexdigest()
assert '"wow.exe"' not in subprocess.check_output(['tasklist', '/fi', 'IMAGENAME eq Wow.exe', '/fo', 'csv', '/nh'], text=True).lower()
for name in ('terrain.bin', 'human-0.mesh', 'WorldNormals.bin', 'ShadowVS.bin', 'ShadowPS.bin', 'ActorShadow.bin', 'ActorTiles.bin', 'RasterVS.bin', 'RasterPS.bin'):
    shutil.copyfile(fixture/name, out/name)
for name, digest in inputs['shaders'].items():
    assert hashlib.sha256((out/(name+'.bin')).read_bytes()).hexdigest() == digest
shutil.copyfile(fp.COMPILED/'WorldNormals.bin', out/'WorldNormalsFixed.bin')
shutil.copyfile(fp.COMPILED/'WorldNormalsNoDistance.bin', out/'WorldNormalsNoDistance.bin')
assets = Assets()
cases = []
try:
    for model in ('3',) if args.quick else ('2', '3'):
        programs = {i: code for i, _, code in variants(assets.read(f'shaders\\vertex\\vs_{model}_0\\diffuse_t1.bls'))}
        for skin, index in ([('skinned', 11)] if args.quick else [('rigid', 1), ('skinned', 11), ('weighted', 21), ('weighted4', 51)]):
            name = f'model-vs{model}-{skin}.bin'
            (out/name).write_bytes(programs[index]);cases.append((f'{model}-{skin}', name))
    programs = {i: code for i, _, code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_opaque.bls'))}
    (out/'ModelPS.bin').write_bytes(programs[0])
    programs = {i: code for i, _, code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_mod.bls'))}
    (out/'ModelAlphaPS.bin').write_bytes(programs[0])
finally:
    assets.close()

env = fp.zig_env()
for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out/name).mkdir(exist_ok=True);env[key] = str(out/name)
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(out))
exe = out/'model-normals.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(fp.TESTS/'model_normals_gpu.cpp'), str(fp.src('world_gi.cpp')),
                '-luser32', '-o', str(exe)], env=env, check=True)
records = []
for tag, source in cases:
    run = subprocess.run([str(exe), str(args.backend.resolve()), str(out)+os.sep, tag, source], cwd=out,
                         env=env, capture_output=True, text=True, timeout=120)
    (out/(tag+'.log')).write_text(run.stdout+run.stderr)
    assert run.returncode == 0, run.stdout+run.stderr
    assert 'MODEL_NORMALS_GPU_PASS' in run.stdout
    assert 'MODEL_NORMALS_ALPHA_PASS' in run.stdout and 'MODEL_NORMALS_VALIDATION_PASS' in run.stdout
    groups = {}
    for row in csv.DictReader((out/(tag+'-samples.csv')).open()):
        if float(row['z']) < 1.65:
            continue
        values = tuple(float(row[k]) for k in ('original', 'geometric', 'fixed'))
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in values)
        groups.setdefault((row['elevation'], row['sample']), {})[int(row['frame'])] = values
    jumps = [tuple(abs(g[f+1][i]-g[f][i]) for i in range(3)) for g in groups.values() for f in g if f+1 in g]
    counts = [sum(j[i] > .25 for j in jumps) for i in range(3)]
    assert len(jumps) == 4377
    assert counts[2] <= counts[1]+5 and counts[2]*3 < counts[0], counts
    samples = [values for g in groups.values() for values in g.values()]
    errors = sorted(abs(v[2]-v[1]) for v in samples)
    disabled = sorted(abs(1-v[1]) for v in samples)
    p95 = int(.95*(len(samples)-1))
    # A disabled shadow pass must fail even though it has zero temporal jumps.
    assert errors[p95] < .03 < .25 < disabled[p95]
    record = {'native_vertex_program': tag, 'head_transitions': len(jumps), 'large_jumps_baseline_control_fixed': counts,
              'fixed_control_error_p95': errors[p95], 'disabled_shadow_error_p95': disabled[p95]}
    records.append(record);print(json.dumps(record), flush=True)
(out/'validation.json').write_text(json.dumps({'status': 'PASS', 'backend': str(args.backend), 'inputs': inputs,
    'game_launched': False, 'cases': records,
    'fixed_shader_sha256': hashlib.sha256((out/'WorldNormalsFixed.bin').read_bytes()).hexdigest()}, indent=2)+'\n')
