#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exercise production shadow selection and bounded source transitions."""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'point-shadow-selection';out.mkdir(parents=True,exist_ok=True)
for opt in ('2','0'):
    exe=out/f'selection-O{opt}.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',f'-O{opt}','-UNDEBUG','-std=c++17',
                    *fp.test_include_flags(),str(fp.TESTS/'test_point_shadow_selection.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
