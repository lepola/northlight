#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual upstream renderer: independent ambient fog, no geometry, reset and MSAA."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'fog-recovery-gpu';out.mkdir(parents=True,exist_ok=False)
env=fp.zig_env()
for key,name in [('TEMP','tmp'),('TMP','tmp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shaders'),('DXVK_STATE_CACHE_PATH','state'),('DXVK_LOG_PATH','logs')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n');env['DXVK_CONFIG_FILE']=str(out/'dxvk.conf')
backend=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
paths=[fp.TESTS/'regional_fog_recovery_gpu.cpp',*[fp.src(n) for n in sources],fp.src('world_renderer.h'),fp.src('regional_fog_cache.h'),fp.src('renderer.cpp'),backend]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in paths};report={'pass':False,'inputs':inputs,'runs':[],'game_launched':False,'private_render_targets':True}
def run(name,command):
    tasks=subprocess.check_output(['tasklist.exe','/FO','CSV','/NH'],text=True)
    assert not any(line.lower().startswith(('"wow.exe"','"wowclient.exe"','"wowclient64.exe"')) for line in tasks.splitlines()),'Game must be closed for isolated GPU tests'
    result=subprocess.run(list(map(str,command)),cwd=out,env=env,capture_output=True,text=True,timeout=240)
    (out/(name+'.log')).write_text(result.stdout+result.stderr);print(name,result.returncode,(result.stdout+result.stderr)[-5000:],flush=True)
    report['runs'].append({'name':name,'exit_code':result.returncode});assert result.returncode==0,name
try:
    exe=out/'fog.exe'
    run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),paths[0],*[fp.src(n) for n in sources],'-luser32','-luuid','-o',exe])
    for samples in [0,2]:
        settings=out/('settings-'+str(samples));settings.mkdir();run('gpu-msaa'+str(samples),[exe,backend,settings,samples])
    report['pass']=True
finally:
    report['inputs_unchanged']=all(sha(Path(path))==value for path,value in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
