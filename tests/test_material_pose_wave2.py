#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Focused CPU-only pose bounds and current-constant regression checks."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='material-pose-wave2-') as tmp:
    for name in ('material_skin_bindings_wave2', 'material_skin_types_wave2'):
        exe = Path(tmp) / (name + '.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                        *fp.test_include_flags(), str(fp.TESTS / (name + '.cpp')), '-o', str(exe)], check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        subprocess.run([str(exe)], check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
