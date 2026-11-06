#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Merged lamp batches preserve material normals, visibility, rejection and constant banks."""
from pathlib import Path
import os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
backend=os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
exe=out/'local-direct-batches.exe'
point_material=int('sampler2D LocalNormals' in fp.src('local_light_effects.hlsl').read_text())
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',
                '-DNORTHLIGHT_POINT_MATERIAL='+str(point_material),*fp.include_flags(),str(fp.TESTS/'local_direct_batches_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe),str(fp.COMPILED),backend],cwd=out,check=True,timeout=90)
host=fp.src('world_renderer.h').read_text()
assert 'd->SetPixelShaderConstantF(84,batch.position[0].data(),2*N);d->SetPixelShaderConstantF(100,batch.color[0].data(),2*N);' in host
source=fp.src('world_effects.hlsl').read_text()
wide=source[source.index('float4 LocalDirect16('):source.index('float3 removalScale(')]
assert 'autoBumpNormal(uv,p,smoothNormal,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz)' in wide
assert 'LampPos[16]:register(c84)' in wide and 'LampColor[16]:register(c100)' in wide
