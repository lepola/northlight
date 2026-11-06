#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Reproduce bounded crowd preparation and its 18-cube draw amplification."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

output = fp.output_dir()
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='northlight-shadow-crowd-', dir=output) as tmp:
    for opt in ('-O2', '-O0'):
        exe = Path(tmp) / 'crowd.exe'
        command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', opt, '-UNDEBUG',
                   *fp.test_include_flags(), str(fp.TESTS / 'test_shadow_crowd.cpp'), '-luser32']
        subprocess.run([*command, '-o', str(exe)], env=fp.zig_env(), check=True)
        subprocess.run([str(exe)], check=True)
