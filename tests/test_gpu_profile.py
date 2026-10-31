#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exercise the real asynchronous profiler, including all four direction history marks."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-gpu-profile-') as tmp:
    for name, flags in (('O2', ['-O2']), ('ASan+UBSan', ['-O1', '-g', '-fsanitize=address,undefined'])):
        exe = Path(tmp)/name
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(fp.TESTS/'support/gpu_profile'), *fp.test_include_flags(),
                        str(fp.TESTS/'test_gpu_profile.cpp'), '-o', str(exe)], check=True)
        print(name, subprocess.check_output([str(exe)], text=True), end='', flush=True)
