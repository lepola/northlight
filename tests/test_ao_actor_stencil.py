#!/usr/bin/env python3
# northlight-test: requires=cxx
"""CPU-only ownership, stencil-bit composition, state/refcount and failure tests."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='ao-actor-stencil-') as temp:
    root = Path(temp)
    shutil.copyfile(here / 'test_ao_actor_stencil_api.h', root / 'd3d9.h')
    for label, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
        executable = root / ('test-' + label)
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(root), *fp.test_include_flags(), str(here / 'test_ao_actor_stencil.cpp'), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, check=True, timeout=60)
        print(label, result.stdout, result.stderr, end='', flush=True)
