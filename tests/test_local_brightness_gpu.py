#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Production WorldComposite bounds overlapping lamps on prelit indoor surfaces."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser()
parser.add_argument('--baseline-bin', type=Path, help='Also prove the previous WorldComposite clips the orange control.')
args = parser.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'))
exe = out / 'local-brightness.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.include_flags(), str(fp.TESTS / 'local_brightness_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe), str(fp.COMPILED), backend, *([str(args.baseline_bin.resolve())] if args.baseline_bin else [])],
               cwd=out, check=True, timeout=90)
