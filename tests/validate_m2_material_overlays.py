# northlight-test: requires=windows-gpu,zig,client manual
"""Native M2 effects must follow the displaced surface for every blend mode."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root), str(root/'renderer')]
import northlight_paths as fp
from extract_signatures import variants
from world_scene_builder import Assets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True, help='source.fgr mip chain and PreviewBase.bin')
parser.add_argument('--quick', action='store_true')
args = parser.parse_args()
subprocess.run(['powershell', '-NoProfile', '-Command',
                "if (Get-Process Wow,WowClassic,WowClient64 -ErrorAction SilentlyContinue) { throw 'Close WoW before the GPU test.' }"], check=True)
out = fp.output_dir()
env = fp.zig_env()
for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out/name).mkdir(exist_ok=True)
    env[key] = str(out/name)
for key in list(env):
    if key.startswith('NORTHLIGHT_TEST_'):
        env.pop(key)
env.update(NORTHLIGHT_TEST_MODEL_OVERLAY='1', NORTHLIGHT_TEST_MESH_GRID='4', DXVK_LOG_PATH=str(out))
exe = out/'m2-effects.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17', '-Wno-microsoft-exception-spec',
                '-DNORTHLIGHT_MATERIAL_MIRROR', '-DNORTHLIGHT_OBLIQUE_WIDTH=320', '-DNORTHLIGHT_OBLIQUE_HEIGHT=200',
                *fp.test_include_flags(), str(fp.TESTS/'auto_normal_oblique_gpu.cpp'), '-luser32', '-luuid', '-o', str(exe)],
               env=env, check=True)
records = []
assets = Assets()
try:
    pixel = next(code for index, _, code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_opaque.bls')) if index == 0)
    for model in ('3',) if args.quick else ('2', '3'):
        base = {index: code for index, _, code in variants(assets.read(f'shaders\\vertex\\vs_{model}_0\\diffuse_t1.bls'))}
        glow = {index: code for index, _, code in variants(assets.read(f'shaders\\vertex\\vs_{model}_0\\diffuse_env.bls'))}
        cases = [('rigid', 1, 'model'), ('single', 11, 'model'), ('weighted', 21, 'model'), ('weighted4', 51, 'model4'),
                 ('rigid-unlit', 0, 'model'), ('single-unlit', 10, 'model'), ('weighted-unlit', 20, 'model'), ('weighted4-unlit', 50, 'model4')]
        for skin, index, mode in [('single', 11, 'model')] if args.quick else cases:
            folder = out/args.backend.stem/f'sm{model}-{skin}'
            folder.mkdir(parents=True, exist_ok=True)
            for fixture in [*args.fixture.glob('source.fgr*'), args.fixture/'PreviewBase.bin']:
                shutil.copyfile(fixture, folder/fixture.name)
            for name, code in [('terrain-vs.bin', base[index]), ('model-glow-vs.bin', glow[index]), ('terrain-ps.bin', pixel)]:
                (folder/name).write_bytes(code)
            run = subprocess.run([str(exe), str(args.backend.resolve()), str(folder)+os.sep, '12', '.7', mode],
                                 cwd=out, env=env, capture_output=True, text=True, timeout=120)
            (folder/'gpu.log').write_text(run.stdout+run.stderr, encoding='utf-8')
            assert run.returncode == 0, run.stdout[-4500:]+run.stderr[-1500:]
            assert 'M2_OVERLAY_SYNC_GPU_PASS' in run.stdout and run.stdout.count('M2_EFFECT_BLEND_PASS') == 5
            records.append(dict(shader_model=model, skin=skin, blend_modes=5, output=run.stdout))
            print(f'M2_EFFECTS_PASS sm{model} {skin} blends=5', flush=True)
finally:
    assets.close()
(out/('validation-'+args.backend.stem+'.json')).write_text(json.dumps(dict(backend=str(args.backend), cases=records), indent=2)+'\n')
