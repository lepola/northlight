#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Bound GI trigonometric differences across the authored angle domain and rounding modes."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix="gi-trig-") as tmp:
    for mode in ('O0','O2'):
        exe=Path(tmp)/(mode+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-'+mode,'-UNDEBUG','-ffp-contract=off','-frounding-math',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
