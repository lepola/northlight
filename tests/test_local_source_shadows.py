#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Check bounded source selection and actual direct/shadow bytecode on a hidden device."""
from pathlib import Path
import os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
for name in ['test_local_source_shadows','local_source_shadows_gpu']:
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    *fp.include_flags(),str(fp.TESTS/(name+'.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
    args=[str(fp.COMPILED),backend] if name.endswith('_gpu') else []
    subprocess.run([str(exe),*args],cwd=out,check=True,timeout=90)
print('PASS bounded source selection and direct-light shadow correction')
