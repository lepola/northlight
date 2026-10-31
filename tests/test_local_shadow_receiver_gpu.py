#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Self-shadow stripes, grazing receivers and real blockers on all cube faces."""
import argparse,ctypes,hashlib,importlib.util,json,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--backend',action='append',required=True);parser.add_argument('--compatible-compiler',action='store_true');args=parser.parse_args()
api=ctypes.WinDLL('kernel32',use_last_error=True);api.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_wchar_p];api.CreateMutexW.restype=ctypes.c_void_p;api.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];api.ReleaseMutex.argtypes=[ctypes.c_void_p];api.CloseHandle.argtypes=[ctypes.c_void_p]
mutex=api.CreateMutexW(None,0,'Local\\NorthlightOfflineGPU')
if not mutex or api.WaitForSingleObject(mutex,0) not in (0,128):raise RuntimeError('Another offline GPU test is active')
try:
    spec=importlib.util.spec_from_file_location('guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
    def check_game():
        processes=guard.processes()
        if any(p['name'].lower().startswith('wow') and p['name'].lower().endswith('.exe') for p in processes):raise RuntimeError('Game process exists; defer GPU tests')
    check_game();out=fp.output_dir()/f'local-shadow-receiver-{time.time_ns()}';out.mkdir(parents=True,exist_ok=False)
    source=out/'receiver.hlsl';source.write_text((fp.SHADERS/'local_light_effects.hlsl').read_text().replace('#include "auto_bump_normal.hlsl"',(fp.SHADERS/'auto_bump_normal.hlsl').read_text())+'''
float4 Origin:register(c80);float4 AxisU:register(c81);float4 AxisV:register(c82);float4 SurfaceNormal:register(c83);
float4 Receiver(float2 uv:TEXCOORD0):COLOR0{float3 p=Origin.xyz+(uv.x-.5)*AxisU.xyz+(uv.y-.5)*AxisV.xyz;return localVisibility(p-LocalPosition.xyz,SurfaceNormal.xyz).xxxx;}
''')
    compiler=Compiler(allow_compatible_binary=args.compatible_compiler)
    for entry,target in [('Receiver','ps_3_0'),('LocalShadowVS','vs_3_0'),('LocalShadowPS','ps_3_0')]:compiler.compile(source,entry,target,out/(entry+'.bin'))
    env=fp.zig_env();flags={'creationflags':subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW}
    for key,name in [('TEMP','temp'),('TMP','temp'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
        (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
    env.update(DXVK_LOG_PATH=str(out),DISABLE_VULKAN_OBS_CAPTURE='1')
    exe=out/'receiver.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'local_shadow_receiver_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True,**flags)
    results=[]
    for index,backend in enumerate(args.backend):
        check_game();run=subprocess.run([str(exe),str(Path(backend).resolve()),str(out)+'/'],cwd=out,env=env,capture_output=True,text=True,timeout=90,**flags)
        (out/f'gpu-{index}.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True)
        if run.returncode or 'failures=0' not in run.stdout:raise RuntimeError(run.stdout+run.stderr)
        results.append({'backend':str(Path(backend).resolve()),'result':run.stdout.strip()})
    report={'game_launched':False,'authored_geometry':True,'production_caster_and_receiver':True,'results':results,'source_sha256':hashlib.sha256((fp.SHADERS/'local_light_effects.hlsl').read_bytes()).hexdigest()}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(out,flush=True)
finally:
    api.ReleaseMutex(mutex);api.CloseHandle(mutex)
