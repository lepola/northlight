#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Moving near casters remain current with retained environment shadows."""
import argparse,ctypes,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
from validate_native_scene_trace import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',action='append',required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);processes()
api=ctypes.WinDLL('kernel32',use_last_error=True)
api.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_wchar_p];api.CreateMutexW.restype=ctypes.c_void_p
api.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];api.ReleaseMutex.argtypes=[ctypes.c_void_p];api.CloseHandle.argtypes=[ctypes.c_void_p]
mutex=api.CreateMutexW(None,0,'Local\\NorthlightOfflineGPU')
if not mutex or api.WaitForSingleObject(mutex,0) not in (0,128):raise RuntimeError('Another offline GPU test is active')
try:
    fp.out=lambda:out/'toolchain'
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    env=fp.zig_env()
    for key,folder in [('TEMP','temp'),('TMP','temp'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
        (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
    env.update(DXVK_LOG_PATH=str(out),DISABLE_VULKAN_OBS_CAPTURE='1')
    exe=out/'near-interval.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),str(fp.TESTS/'near_shadow_interval_gpu.cpp'),*(str(fp.src(s)) for s in sources),'-luser32','-luuid','-o',str(exe)]
    with (out/'compile.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    results=[]
    for index,backend in enumerate(a.backend):
        processes();run=subprocess.run([str(exe),str(Path(backend).resolve())],cwd=out,env=env,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
        text=run.stdout+run.stderr;(out/f'gpu-{index}.log').write_text(text);print(text,flush=True)
        assert run.returncode==0 and 'NEAR_INTERVAL_PASS' in text
        results.append({'backend':backend,'output':text})
    (out/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'results':results},indent=2)+'\n')
finally:
    api.ReleaseMutex(mutex);api.CloseHandle(mutex)
