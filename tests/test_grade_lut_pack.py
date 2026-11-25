#!/usr/bin/env python3
# northlight-test: requires=cxx
"""CPU tests for the runtime LUT pack index and lazy table verifier."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

HERE = Path(__file__).resolve().parent

with tempfile.TemporaryDirectory(prefix='northlight-grade-lut-pack-') as tmp:
    tmp = Path(tmp)
    exe = tmp / 'test-grade-lut-pack'
    subprocess.run([
        'clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
        *fp.test_include_flags(), str(HERE / 'test_grade_lut_pack.cpp'), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe), str(tmp / 'fixture.nllutpack')], check=True)
