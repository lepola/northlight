#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Bounded scratch storage: exact eviction, all I/O failures, allocation rollback."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='temporary-geometry-') as folder:
    for optimization in ['-O0','-O2']:
        exe=Path(folder)/'test.exe'
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',optimization,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_temporary_geometry.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
