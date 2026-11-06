#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Independent WMO colour interpolation and shader dependency regressions."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'wmo-vertex-color'
out.mkdir(parents=True, exist_ok=True)
for mode in ('O0', 'O2'):
    exe = out / (mode + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-' + mode, '-UNDEBUG', *fp.include_flags(),
                    str(fp.TESTS / 'wmo_vertex_color.cpp'), '-luser32', '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    (out / (mode + '.log')).write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.stdout + run.stderr
    print(mode, run.stdout, flush=True)
