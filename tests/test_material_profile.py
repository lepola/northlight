#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""CPU attribution, asynchronous GPU spans, bounded resources and disabled cost."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.out()/'material-profile';out.mkdir(exist_ok=True);exe=out/'profile.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-I'+str(fp.TESTS/'support/gpu_profile'),*fp.test_include_flags(),str(fp.TESTS/'test_material_profile.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
