#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Byte-exact subdivision regression and optional paired CPU benchmark."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'displacement-subdivision'
out.mkdir(parents=True, exist_ok=True)
for mode in ('O2', 'O0'):
    exe = out / (mode + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-' + mode, '-UNDEBUG',
                    *fp.include_flags(), str(fp.TESTS / 'displacement_subdivision.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe), *(['--benchmark'] if mode == 'O2' and '--benchmark' in sys.argv else [])], capture_output=True, text=True)
    (out / (mode + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    print(mode, result.stdout, flush=True)
