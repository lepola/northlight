#!/usr/bin/env python3
# northlight-test: requires=zig
"""HDR transfer preserves native MRT outputs and rejects output collisions, without a GPU."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('hdr_ui_patch.h').read_text()
fixed = 'r&&r==linearOutput'
before = 'r&&(!linearOutput||r==linearOutput)'
assert source.count(fixed) == 1
with tempfile.TemporaryDirectory(prefix='northlight-hdr-ui-patch-') as tmp:
    folder = Path(tmp)
    compiler = [str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu'] if os.name == 'nt' else ['clang++']
    for label, header in [('fixed', source), ('before-fix', source.replace(fixed, before))]:
        (folder/'hdr_ui_patch.h').write_text(header)
        exe = folder/(label + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([*compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(folder), str(fp.TESTS/'test_hdr_ui_patch.cpp'), '-o', str(exe)],
                       check=True, env=fp.zig_env() if os.name == 'nt' else None)
        run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
        if label == 'before-fix':
            assert run.returncode != 0 and 'accepted==!collision' in run.stderr, run.stdout+run.stderr
            print('before-fix: expected auxiliary-output acceptance assertion failed')
        else:
            assert run.returncode == 0, run.stdout+run.stderr
            print(run.stdout, end='')
