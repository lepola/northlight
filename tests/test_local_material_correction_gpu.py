# northlight-test: requires=windows-gpu,zig manual
"""Actual point/material correction bytecode with distinct emitters and neutral detail."""
import argparse, os, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',type=Path,required=True);a=p.parse_args()
tasks=subprocess.check_output(['tasklist.exe','/FO','CSV','/NH'],text=True).lower()
assert not any('"'+n+'"' in tasks for n in ('wow.exe','wowclient.exe','wowclient64.exe'))
out=fp.output_dir();exe=out/'local-material-correction.exe'
env=fp.zig_env();env['DXVK_CONFIG']='d3d9.customVendorId = 1002';env['DISABLE_VULKAN_OBS_CAPTURE']='1'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'local_material_correction_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
r=subprocess.run([str(exe),str(a.backend.resolve()),str(fp.COMPILED/'LocalLighting.bin')],cwd=out,env=env,capture_output=True,text=True,timeout=90)
(out/'gpu.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr);assert r.returncode==0
assert 'LOCAL_MATERIAL_CORRECTION_PASS cases=48 ' in r.stdout
