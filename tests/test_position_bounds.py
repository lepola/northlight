#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Conservative position-only admission with tracked writes and bounded storage."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=out/'position-bounds.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'position_bounds.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
