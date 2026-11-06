#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Rigid envelope bounds: exact slow-path equivalence and vertex enclosure."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='rigid-bounds-',dir=fp.output_dir()) as tmp:
    for opt in ('-O2','-O0'):
        exe=Path(tmp)/'rigid.exe'
        command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-UNDEBUG',
                 *fp.test_include_flags(),fp.TESTS/'test_replay_rigid_bounds.cpp','-luser32','-o',exe]
        subprocess.run(list(map(str,command)),env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
