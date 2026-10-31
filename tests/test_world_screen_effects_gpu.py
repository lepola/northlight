#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production fog/material screen passes without a geometry publication, without the game."""
from pathlib import Path
import hashlib,importlib.util,json,os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
repo=fp.REPO
out=fp.output_dir()/'world-screen-gpu';out.mkdir(parents=True,exist_ok=False);(out/'settings').mkdir()
spec=importlib.util.spec_from_file_location('screen_guard',repo/'tests/validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
backend=Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache'),('__GL_SHADER_DISK_CACHE_PATH','driver-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DXVK_CONFIG='d3d9.customVendorId = 1002',DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
inputs={str(p):sha(p) for p in [fp.TESTS/'world_screen_effects_gpu.cpp',*(fp.src(s) for s in sources),fp.src('hdr_compiled_shaders.h'),fp.src('world_renderer.h'),fp.src('renderer.cpp'),fp.src('settings_service.h'),backend]}
report={'pass':False,'inputs':inputs,'commands':[],'game_launched':False,'actual_world_renderer':True,'private_render_target':True,'geometry_available':False}
def run(label,command,extra=None):
    guard.processes();p=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(extra or {})},capture_output=True,text=True,timeout=240)
    (out/(label+'.log')).write_text(p.stdout+p.stderr);report['commands'].append({'label':label,'exit_code':p.returncode});print(label,p.returncode,(p.stdout+p.stderr)[-6000:],flush=True);assert p.returncode==0,label
try:
    exe=out/'world-screen-gpu.exe'
    run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),'-I'+str(fp.TESTS),fp.TESTS/'world_screen_effects_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe])
    for samples in [0,2]:
        settings=out/('settings-msaa'+str(samples));settings.mkdir()
        run('gpu-msaa'+str(samples),[exe,backend,settings],{'NORTHLIGHT_TEST_MSAA':str(samples)})
    report['pass']=True
finally:
    guard.processes();report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
