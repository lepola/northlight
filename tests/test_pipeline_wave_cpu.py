#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Production CPU bookkeeping contracts, compiled for the renderer's x86 ABI."""
from pathlib import Path
import subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import replay_gpu_fixture as gpu
out=fp.out()/'pipeline-wave-cpu'
out.mkdir(parents=True, exist_ok=True)
stub=gpu.stub
a=stub.index('using UINT'); b=stub.index('constexpr unsigned D3DUSAGE')
(out/'d3d9.h').write_text(stub[:a]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[b:])
(out/'replay_fixture.h').write_text(gpu.fixture)
for opt in ('-O0','-O2'):
    exe=out/('pipeline-'+opt[1:]+'.exe')
    cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'pipeline_wave_cpu.cpp'),'-o',str(exe)]
    subprocess.run(cmd,env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
