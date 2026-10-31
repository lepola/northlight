#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Production material codec: same surface, different camera rotations; no game."""
import argparse, importlib.util, os, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--compatible-compiler',action='store_true');args=parser.parse_args()
out=fp.output_dir()/'material-frame-rotation';out.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('rotation_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
guard.processes()
compiler=Compiler(allow_compatible_binary=args.compatible_compiler)
source=out/'codec.hlsl'
capture=(fp.SHADERS/'auto_bump_capture.hlsl').read_text().replace('#include "auto_parallax.hlsl"',(fp.SHADERS/'auto_parallax.hlsl').read_text())
source.write_text(capture+'''
float4 Reference:register(c80);float4 Mapped:register(c81);
MaterialValue fixture(){MaterialValue v;v.reference=Reference.xyz;v.normal=Mapped.xyz;v.occlusion=1;v.uv=v.uv1=0;return v;}
CaptureResult Legacy(float2 uv:TEXCOORD0){
    MaterialValue v=fixture();float3 delta=v.normal-v.reference;
    float along=mad(dot(v.reference.xy,delta.xy),rcp(max(1+v.reference.z,1e-4)),delta.z);
    float3 detail=float3(mad(-v.reference.xy,along,delta.xy),max(dot(v.reference,v.normal),0));
    float2 encoded=mad(detail.xy,rcp(max(dot(abs(detail),1),1e-6))*.5,.5);
    CaptureResult o;o.normal=float4(encoded,4,1);o.occlusion=1;return o;
}
CaptureResult World(float2 uv:TEXCOORD0){return captureValue(fixture(),float4(0,0,0,4),1);}
sampler2D BumpMaterial:register(s9);float4 BumpControl:register(c82);float4 SceneNormal:register(c83);float4 ViewRows[3]:register(c84);
'''+(fp.SHADERS/'auto_bump_normal.hlsl').read_text()+'''
float4 Decode(float2 uv:TEXCOORD0):COLOR0{return float4(autoBumpNormal(uv,0,SceneNormal.xyz,4,ViewRows[0].xyz,ViewRows[1].xyz,ViewRows[2].xyz),1);}
''')
for entry,name in [('Legacy','capture-legacy'),('World','capture-world'),('Decode','decode')]:compiler.compile(source,entry,'ps_3_0',out/(name+'.bin'))
env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache'),('__GL_SHADER_DISK_CACHE_PATH','driver-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
exe=out/'rotation.exe'
subprocess.run(list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),fp.TESTS/'material_frame_rotation_gpu.cpp','-luser32','-o',exe])),env=env,check=True)
guard.processes()
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))
run=subprocess.run([str(exe),str(backend),str(out)+'/'],cwd=out,env=env,capture_output=True,text=True,timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True);assert run.returncode==0,run.stdout+run.stderr
assert 'MATERIAL_FRAME_ROTATION_PASS' in run.stdout
