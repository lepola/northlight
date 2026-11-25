#!/usr/bin/env python3
# northlight-test: requires=cxx
"""CPU checks for the shared regional 3D colour grade and legacy curve fallback."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-world-tone-curves-') as tmp:
    exe = Path(tmp) / 'test-world-tone-curves'
    subprocess.run([
        'clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
        *fp.test_include_flags(), str(HERE / 'test_world_tone_curves.cpp'), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
