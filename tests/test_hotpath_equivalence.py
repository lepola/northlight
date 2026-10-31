#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Compare optimized production algorithms to the frozen 9537631 implementations."""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-hotpaths-') as tmp:
    for name, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        exe = Path(tmp) / name
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        *fp.test_include_flags(), str(fp.TESTS / 'test_hotpath_equivalence.cpp'), '-o', str(exe)], check=True)
        print(name, subprocess.check_output([str(exe)], text=True), end='', flush=True)
