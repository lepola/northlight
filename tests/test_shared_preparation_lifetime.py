#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Shared mesh preparation exactness, LRU and publication-lifetime regression."""
from pathlib import Path
import subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.out()/"shared-preparation-lifetime";out.mkdir(parents=True,exist_ok=True)
for mode in ("O2","O0"):
    exe=out/(mode+".exe")
    subprocess.run([str(fp.zig()),"c++","-target","x86-windows-gnu","-std=c++17","-"+mode,"-UNDEBUG","-Wall","-Wextra","-Werror",*fp.test_include_flags(),str(fp.TESTS/"test_shared_preparation_lifetime.cpp"),str(fp.src("world_mesh_plan.cpp")),str(fp.src("world_gi.cpp")),"-o",str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    (out/(mode+".log")).write_bytes((result.stdout+result.stderr).encode());print(mode,result.stdout+result.stderr,end="");result.check_returncode()
