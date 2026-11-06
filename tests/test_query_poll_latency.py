#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Bounded query polling and query/replay CPU attribution; no game or GPU."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-query-profile-') as temporary:
    for label, flags in (('O2', ['-O2']), ('debug', ['-O0', '-g'])):
        binary = Path(temporary)/(label+'.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                        '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                        *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')),
                        '-o', str(binary)], env=fp.zig_env(), check=True)
        subprocess.run([str(binary)], check=True)
print('PASS query polling and exact non-overlapping wait attribution, O2/debug; no GPU.')
