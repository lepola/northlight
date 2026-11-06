#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Exact material-map uploads with bounded scratch reuse and nonblocking failures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir().resolve()
exe = out / 'material-upload.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                '-std=c++17', '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
                str(fp.TESTS/'material_disk_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'))
with tempfile.TemporaryDirectory(prefix='uploads-', dir=out) as folder:
    run = subprocess.run([str(exe), backend, folder, 'uploads'], cwd=out, text=True, capture_output=True, timeout=90)
    (out/'gpu.log').write_text(run.stdout+run.stderr, encoding='utf-8')
    print(run.stdout+run.stderr, end='')
    run.check_returncode()
    assert 'MATERIAL_UPLOAD_REUSE_PASS' in run.stdout
