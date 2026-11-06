#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare the M2 normal-only capture with the generic capture at disabled POM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.SCRIPTS / 'shaders'))
from shader_compiler import Compiler, validate_assembly
from validate_native_scene_trace import processes

p = argparse.ArgumentParser(__doc__)
p.add_argument('--compatible-compiler', action='store_true')
p.add_argument('--backend', type=Path, default=fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll')
args = p.parse_args()
processes()
out = fp.output_dir() / 'material-normal-native'
out.mkdir(parents=True, exist_ok=True)
compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
vertex = out / 'vertex.hlsl'
vertex.write_text('''float4 Control:register(c0);
struct O{float4 p:POSITION;float2 uv:TEXCOORD0;float4 clip:TEXCOORD7;float4 color:COLOR0;};
O VS(float4 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=p;o.uv=uv;
float z=4+8*Control.y;o.clip=float4(p.x*z*(1+Control.w),p.y*z,0,z*(1+p.x*Control.z*.3));o.color=float4(1,1,1,Control.x);return o;}''')
compiler.compile(vertex, 'VS', 'vs_3_0', out / 'vs.bin')
stats = {}
for entry in ('BumpNative', 'BumpNormalNative'):
    compiler.compile(fp.SHADERS / 'auto_bump_capture.hlsl', entry, 'ps_3_0', out / (entry + '.bin'))
    stats[entry] = validate_assembly(out / (entry + '.bin.asm'), 'ps_3_0')
assert stats['BumpNormalNative']['static_instruction_slots'] < stats['BumpNative']['static_instruction_slots']
exe = out / 'compare.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(fp.TESTS / 'material_early_alpha_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
env = fp.zig_env()
env.update(DXVK_LOG_PATH=str(out), DXVK_STATE_CACHE_PATH=str(out), DXVK_SHADER_CACHE_PATH=str(out))
processes()
runs = []
for mode in ('--normal-only', '--normal-only-production'):
    result = subprocess.run([str(exe), str(out / 'BumpNative.bin'), str(out / 'BumpNormalNative.bin'),
                             str(out / 'vs.bin'), str(args.backend), mode],
                            env=env, cwd=out, text=True, capture_output=True, timeout=120)
    (out / (mode.lstrip('-') + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0 and 'SHADER_MICRO_PASS cases=128' in result.stdout, result.stdout + result.stderr
    runs.append({'mode': mode, 'output': result.stdout})
    print(mode, result.stdout)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
(out / 'validation.json').write_text(json.dumps({
    'pass': True, 'compiler': compiler.provenance, 'stats': stats,
    'backend': str(args.backend), 'backend_sha256': sha(args.backend),
    'source_sha256': sha(fp.SHADERS / 'auto_bump_capture.hlsl'),
    'runs': runs, 'game_launched': False,
    'limitation': 'Synthetic component comparison; not in-game FPS.'}, indent=2) + '\n')
