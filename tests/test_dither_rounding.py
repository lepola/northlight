#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Final 8-bit dither preserves exact colours and fractional gradient averages."""
import sys
from pathlib import Path
import subprocess
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-dither-') as tmp:
    executable = Path(tmp) / 'rounding'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
