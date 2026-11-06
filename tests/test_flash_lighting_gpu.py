#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Shipped flash bytecode against the CPU distance/energy contract on a real GPU."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'flash-lighting';out.mkdir(parents=True,exist_ok=True)
exe=out/'flash.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type',*fp.include_flags(),str(fp.TESTS/'flash_lighting_gpu.cpp'),'-luser32','-o',str(exe)],check=True,env=fp.zig_env())
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(Path(os.environ['SystemRoot'])/'SysWOW64'/'d3d9.dll'))
subprocess.run([str(exe),str(fp.COMPILED),backend],check=True,timeout=90)
