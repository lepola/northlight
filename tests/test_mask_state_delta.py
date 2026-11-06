#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Production mask helpers preserve states and skip redundant backend writes."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
source = fp.src('renderer.cpp').read_text()
start = source.index('template<size_t N> bool maskApply(')
end = source.index('    bool nameMaskFullViewport(', start)
(out / 'mask_state_methods.inc').write_text(source[start:end])
exe = out / 'mask-state-delta.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                '-UNDEBUG', '-I', str(out), *fp.test_include_flags(),
                str(fp.TESTS / 'mask_state_delta.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'))
run = subprocess.run([str(exe), backend], cwd=out, text=True, capture_output=True, timeout=60)
(out / 'gpu.log').write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end='')
run.check_returncode()
