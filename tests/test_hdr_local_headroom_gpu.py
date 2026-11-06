#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""HDR receivers retain local-light contrast above SDR white; SDR remains bounded."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser(__doc__)
p.add_argument('--shaders', type=Path, default=fp.COMPILED)
p.add_argument('--backend', type=Path, action='append')
a = p.parse_args()
from validate_native_scene_trace import processes
processes(False)
out = fp.output_dir(); exe = out/'hdr-local-headroom.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           *fp.test_include_flags(), fp.TESTS/'hdr_local_headroom_gpu.cpp', '-luser32', '-o', exe]
with (out/'compile.log').open('w') as log:
    subprocess.run(list(map(str, command)), check=True, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, timeout=180)
backends = a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index, backend in enumerate(backends):
    result = subprocess.run([str(exe), str(a.shaders.resolve()), str(backend.resolve())], cwd=out,
                            env={**fp.zig_env(), 'DXVK_LOG_PATH': str(out)}, capture_output=True, text=True, timeout=180)
    (out/f'gpu-{index}.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr, end='', flush=True)
    assert result.returncode == 0 and 'HDR_LOCAL_HEADROOM_PASS' in result.stdout
processes(False)
