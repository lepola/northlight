#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""GPU regression for rain/snow around foreground mushroom and roof silhouettes."""
from pathlib import Path
import argparse
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--backend', type=Path, help='System D3D9 by default; optional plain x86 DXVK DLL')
args = parser.parse_args()
if os.name != 'nt':
    raise SystemExit('This GPU regression requires Windows')
backend = args.backend or Path(os.environ['WINDIR']) / 'SysWOW64' / 'd3d9.dll'
out = fp.output_dir()
exe = out / 'weather-occlusion.exe'
world = fp.src('world_renderer.h').read_text(encoding='utf-8')
start = world.index('struct NameMaskCompositeBind {')
end = world.index('\n};', start) + 3
(out / 'lens_name_mask.h').write_text('#include "name_stencil.h"\n' + world[start:end] + '\n', encoding='utf-8')
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-I', str(out), *fp.test_include_flags(), str(fp.TESTS / 'weather_occlusion_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
subprocess.run([str(exe), str(backend.resolve())], cwd=out, check=True, timeout=180)
