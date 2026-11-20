#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production water haze range and fog/ownership transitions on the GPU."""
from pathlib import Path
import os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
exe=out/'water-horizon.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                '-Wno-return-type',*fp.include_flags(),str(fp.TESTS/'water_horizon_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
subprocess.run([str(exe),backend],cwd=out,check=True,timeout=90)
