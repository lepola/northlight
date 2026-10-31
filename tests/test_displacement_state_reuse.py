#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Reject stale displacement constants and declaration identities through production read paths."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=out/'displacement-state-reuse.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'displacement_state_reuse.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
