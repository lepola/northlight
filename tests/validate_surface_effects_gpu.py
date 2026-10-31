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
parser.add_argument('--build-only', action='store_true')
args = parser.parse_args()
if os.name != 'nt': raise SystemExit('This manual GPU oracle requires Windows')
folder = fp.out() / 'surface-gpu'
folder.mkdir(exist_ok=True)
exe = folder / 'surface-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-Wall', '-Wextra',
                *fp.test_include_flags(), str(fp.TESTS / 'surface_effects_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
if args.build_only:
    print('GPU image oracle compiled; not run')
    sys.exit(0)
# Do not run the offscreen device while the game is active.
guard = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command',
                        'if (Get-Process -Name wow,WowClient64 -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }'],
                       creationflags=subprocess.CREATE_NO_WINDOW)
if guard.returncode: raise SystemExit('Game process detected; offscreen GPU test deferred')
subprocess.run([str(exe), str(args.backend.resolve())], cwd=folder, check=True, timeout=300)
