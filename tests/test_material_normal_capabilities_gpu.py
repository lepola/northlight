#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,windows-gpu manual
"""Normal-map capability memoization: actual D3D9 pixels and failure retry."""
import subprocess,sys,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
out=fp.output_dir()/'normal-capabilities';out.mkdir(parents=True,exist_ok=True);exe=out/'test.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type',*fp.test_include_flags(),str(fp.TESTS/'material_normal_capabilities_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
env=fp.zig_env();env.update(DXVK_LOG_PATH=str(out),DXVK_STATE_CACHE_PATH=str(out),DXVK_SHADER_CACHE_PATH=str(out));r=subprocess.run([str(exe),str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')],env=env,cwd=out,capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);print(r.stdout);assert r.returncode==0,r.stderr
