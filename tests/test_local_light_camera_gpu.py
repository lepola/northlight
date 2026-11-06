#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""World-fixed lamp irradiance survives horizontal/vertical camera rotation."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'local-light-camera'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'camera.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                '-O2', '-UNDEBUG', '-Wno-return-type', *fp.include_flags(),
                str(fp.TESTS / 'local_light_camera_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'))
command = [str(exe), str(fp.COMPILED), backend]
subprocess.run(command, cwd=out, check=True, timeout=90)
negative = subprocess.run([*command, 'wrong-view'], cwd=out, capture_output=True, text=True, timeout=90)
assert negative.returncode == 1 and 'FAIL LocalDirect' in negative.stdout, negative.stdout + negative.stderr
stale = subprocess.run([*command, 'stale-pose'], cwd=out, capture_output=True, text=True, timeout=90)
assert stale.returncode == 1 and 'FAIL moving pose' in stale.stdout, stale.stdout + stale.stderr
print('PASS: all camera/animation/attachment poses; broken inverse view and stale pose rejected')
