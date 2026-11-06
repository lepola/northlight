#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Exact MRT coverage for early material alpha rejection; hidden device, no game."""
import argparse,hashlib,importlib.util,json,os,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1])]
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler,validate_assembly

p=argparse.ArgumentParser(__doc__);p.add_argument('--compatible-compiler',action='store_true');a=p.parse_args()
out=fp.output_dir()/'material-early-alpha';out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('gpu_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard);guard.processes()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
baseline=fp.TESTS/'fixtures/reference/auto_bump_capture-dbda9f3.hlsl'
assert sha(baseline)=='3f289b828f4a45bd037eee9c44bcc1d0fb3d0da921a9eced9ec5d13226816f0e'
watched=[baseline,fp.SHADERS/'auto_bump_capture.hlsl',fp.SHADERS/'auto_parallax.hlsl',fp.TESTS/'material_early_alpha_gpu.cpp',fp.TESTS/'composite_distance_gpu.cpp',Path(__file__)]
inputs={str(path):sha(path) for path in watched};compiler=Compiler(allow_compatible_binary=a.compatible_compiler)
vertex=out/'vertex.hlsl'
vertex.write_text('''float4 Control:register(c0);struct O{float4 p:POSITION;float2 uv:TEXCOORD0;float2 mask:TEXCOORD1;float3 normal:TEXCOORD5;float4 clip:TEXCOORD7;float4 color:COLOR0;float4 fade:COLOR1;};O VS(float4 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=p;o.uv=uv;o.mask=uv;o.normal=float3(0,0,1);o.clip=float4(p.xy*4,0,4);o.color=float4(1,1,1,Control.x);o.fade=Control.x;return o;}''')
compiler.compile(vertex,'VS','vs_3_0',out/'vs.bin')
for name,source in [('reference',baseline),('current',fp.SHADERS/'auto_bump_capture.hlsl')]:
    folder=out/name;folder.mkdir(exist_ok=True)
    (folder/'auto_bump_capture.hlsl').write_bytes(source.read_bytes())
    (folder/'auto_parallax.hlsl').write_bytes((fp.SHADERS/'auto_parallax.hlsl').read_bytes())
exe=out/'micro.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'material_early_alpha_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
env=fp.zig_env();env.update(DXVK_LOG_PATH=str(out),DXVK_STATE_CACHE_PATH=str(out),DXVK_SHADER_CACHE_PATH=str(out))
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))
runs=[]
for entry in ['BumpGrass','BumpTerrainLayer','BumpNative','BumpNormalGrass']:
    for name in ['reference','current']:
        compiler.compile(out/name/'auto_bump_capture.hlsl',entry,'ps_3_0',out/(name+'.bin'))
        validate_assembly(out/(name+'.bin.asm'),'ps_3_0')
    guard.processes()
    result=subprocess.run([str(exe),str(out/'reference.bin'),str(out/'current.bin'),str(out/'vs.bin'),str(backend)],env=env,cwd=out,text=True,capture_output=True,timeout=120)
    (out/(entry+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0 and 'SHADER_MICRO_PASS cases=16' in result.stdout,result.stdout+result.stderr
    runs.append({'entry':entry,'exact_cases':16,'stdout':result.stdout})
    print(entry,'PASS exact normal/occlusion targets',flush=True)
assert all(sha(Path(path))==digest for path,digest in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'compiler':compiler.provenance,'backend':str(backend),'backend_sha256':sha(backend),'runs':runs,'game_launched':False,'limitation':'Authored coverage cases; GPU timings are component observations, not a game FPS estimate.'},indent=2)+'\n')
