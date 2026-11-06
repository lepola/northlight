# northlight-test: requires=windows-gpu,zig
"""GPU regression for GI outside resident probes; optional alternating benchmark."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser()
baselines = p.add_mutually_exclusive_group()
baselines.add_argument('--baseline', type=Path)
baselines.add_argument('--baseline-ref', help='Read the negative-control WorldGI bytecode from this Git revision.')
p.add_argument('--benchmark', action='store_true')
args = p.parse_args()
if args.benchmark and not (args.baseline or args.baseline_ref):
    p.error('--benchmark requires --baseline or --baseline-ref')
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
if args.baseline_ref:
    args.baseline = out / 'baseline'
    args.baseline.mkdir(exist_ok=True)
    (args.baseline / 'WorldGI.bin').write_bytes(subprocess.check_output(
        ['git', 'show', args.baseline_ref + ':shaders/compiled/WorldGI.bin'], cwd=fp.REPO))
env = fp.zig_env()
for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out/name).mkdir(exist_ok=True)
    env[key] = str(out/name)
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(out))
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'))
exe = out / 'gi-fallback.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.include_flags(), str(fp.TESTS / 'gi_fallback_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=env, check=True)
result = subprocess.run([str(exe), str(fp.COMPILED), backend,
                *([str(args.baseline.resolve())] if args.baseline else []),
                *(['--benchmark'] if args.benchmark else [])], cwd=out, env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
(out / 'gpu.log').write_text(result.stdout, encoding='utf-8')
print(result.stdout, end='')
result.check_returncode()
