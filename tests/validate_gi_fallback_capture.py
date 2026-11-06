# northlight-test: requires=windows-gpu,zig,world-cache
"""Replay the GI fallback delta on a diagnostic capture (explicit reconstruction)."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--capture', type=Path, required=True, help='Prefix ending in capture-1-')
p.add_argument('--baseline', type=Path, required=True, help='Old WorldGI.bin directory')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--shaders', type=Path, default=fp.COMPILED)
p.add_argument('--backend', type=Path, default=Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll')
args = p.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
env = fp.zig_env()
for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local')]:
    (out / name).mkdir(exist_ok=True)
    env[key] = str(out / name)
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(out))
exe = out / 'gi-capture-replay.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.include_flags(), str(fp.TESTS / 'gi_capture_replay.cpp'),
                str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)], env=env, check=True)
result = subprocess.run([str(exe), str(args.capture.resolve()), str(fp.world_cache()),
                         str(args.baseline.resolve()), str(args.shaders.resolve()), str(args.backend.resolve()), str(out)],
                        cwd=out, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
(out / 'replay.log').write_text(result.stdout, encoding='utf-8')
print(result.stdout)
result.check_returncode()
from gi_capture_report import export
export(args.capture.resolve(), args.baseline.resolve(), args.shaders.resolve(), out)
