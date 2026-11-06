#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig manual
"""Pixel-exact production shadow shaders with legacy32B and compact20B live vertices."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', type=Path, required=True)
args=parser.parse_args()
out=fp.out()/'live-terrain-layout'
out.mkdir(parents=True, exist_ok=True)
exe=out/'layout.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(), str(fp.TESTS/'live_terrain_layout_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
result=subprocess.run([str(exe), str(args.backend.resolve())], capture_output=True, text=True)
print(result.stdout, end='')
print(result.stderr, end='', file=sys.stderr)
result.check_returncode()
assert 'LIVE_TERRAIN_LAYOUT_GPU_PASS comparisons=96' in result.stdout
(out/'validation.json').write_text(json.dumps({'pass': True, 'exactComparisons': 96, 'samples': [0,2,4,8],
    'gameLaunched': False, 'output': result.stdout}, indent=2)+'\n', encoding='utf-8')
