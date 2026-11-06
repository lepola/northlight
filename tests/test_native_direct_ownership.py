#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact current scene/native slot matching; renderer direct remains unchanged."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();exe=out/'native-direct-ownership.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_native_direct_ownership.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
