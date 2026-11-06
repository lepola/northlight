#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=windows-exe,zig
"""CPU geometry, resident snapshot and equipment-atlas contracts on Windows."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.out()/'material-cpu'
out.mkdir(parents=True, exist_ok=True)
for name, source, extra in (
    ('geometry', 'test_displacement_geometry.cpp', []),
    ('snapshot', 'displacement-snapshot/test.cpp', ['-I'+str(fp.TESTS/'displacement-snapshot')]),
    ('uploads', 'test_material_texture_uploads.cpp', []),
):
    exe = out/(name+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    *extra, *fp.test_include_flags(), str(fp.TESTS/source), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
