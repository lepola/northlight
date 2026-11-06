#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Native emission preserves atmospheric in-scattering on captured lamp spectra."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser();p.add_argument('--shaders',type=Path,default=fp.COMPILED);args=p.parse_args()
out=fp.output_dir();exe=out/'additive-atmosphere.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.include_flags(),str(fp.TESTS/'additive_atmosphere_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'))
run=subprocess.run([str(exe),str(args.shaders.resolve()),backend],cwd=out,text=True,capture_output=True,timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
assert 'ADDITIVE_ATMOSPHERE_PASS' in run.stdout
