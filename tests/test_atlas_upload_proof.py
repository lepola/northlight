#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=zig
"""Bounded diagnostic upload provenance, no client or GPU needed."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-atlas-proof-') as tmp:
    for optimize in ('-O0', '-O2'):
        exe = Path(tmp)/('test.exe' if sys.platform == 'win32' else 'test')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', optimize, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(), str(fp.TESTS/'test_atlas_upload_proof.cpp'), '-o', str(exe)],
                       check=True, env=fp.zig_env())
        subprocess.run([str(exe)], check=True)
