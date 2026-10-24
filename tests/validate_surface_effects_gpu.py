"""Manual Windows offscreen GPU oracle; no game process, game code or visible window."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser()
parser.add_argument('--backend', type=Path, required=True, help='System D3D9 or a plain DXVK DLL, never the Northlight proxy')
args = parser.parse_args()
if os.name != 'nt': raise SystemExit('This manual GPU oracle requires Windows')
folder = fp.out() / 'surface-gpu'
folder.mkdir(exist_ok=True)
exe = folder / 'surface-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-Wall', '-Wextra',
                *fp.test_include_flags(), str(fp.TESTS / 'surface_effects_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
subprocess.run([str(exe), str(args.backend.resolve())], cwd=folder, check=True, timeout=90)
