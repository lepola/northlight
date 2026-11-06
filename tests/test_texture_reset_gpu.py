#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""A real native Reset preserves CPU copies of surviving managed uploads."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer = fp.src('renderer.cpp').read_text()
reset = renderer[renderer.index('HRESULT STDMETHODCALLTYPE Reset('):renderer.index('    void trimMemory(')]
assert 'NorthlightParticleTextures::cache().reset();NorthlightMaterialTextures::cache().reset();' in reset
out = fp.output_dir(); exe = out/'texture-reset.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
           *fp.test_include_flags(), fp.TESTS/'texture_reset_gpu.cpp', '-luser32', '-o', exe]
with (out/'compile.log').open('w') as log:
    subprocess.run(list(map(str, command)), check=True, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, timeout=180)
backend = Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'
for legacy in (True, False):
    result = subprocess.run([str(exe), str(backend), *(['legacy-clear'] if legacy else [])], cwd=out,
                            capture_output=True, text=True, timeout=60)
    (out/('baseline.log' if legacy else 'gpu.log')).write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr, end='', flush=True)
    if legacy:
        assert result.returncode != 0 and 'TEXTURE_RESET_MANAGED_MISS' in result.stdout
    else:
        assert result.returncode == 0 and 'TEXTURE_RESET_GPU_PASS' in result.stdout
