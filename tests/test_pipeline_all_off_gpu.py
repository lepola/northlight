#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual Device: F10/F11 CPU draw overhead, native pixels and worker pause/resume."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,statistics,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--allow-running-game',action='store_true',default=os.environ.get('NORTHLIGHT_TEST_ALLOW_RUNNING_GAME')=='1',help='Explicitly allow this independent stand to coexist with WoW')
args=parser.parse_args()

out=fp.output_dir()/'all-off';out.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('all_off_guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
backend=Path(os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')))
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n',encoding='utf-8')
env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG='d3d9.customVendorId = 1002',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
baseline=os.environ.get('NORTHLIGHT_PIPELINE_BASELINE');roots=[('current',fp.REPO)]+([('baseline',Path(baseline))] if baseline else [])
report={'pass':False,'game_launched':False,'actual_device':True,'allow_running_game':args.allow_running_game,'native_draw_stub_for_cpu_only':True,'actual_gpu_for_images':True,'commands':[],'timings':{}}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in [fp.TESTS/'pipeline_all_off_gpu.cpp',fp.src('renderer.cpp'),fp.src('world_renderer.h'),fp.src('quality_settings.h'),backend]}
report['inputs']=inputs;report['frame_timings']={}
def run(label,command,cwd):
    guard.processes(allow_game=args.allow_running_game);r=subprocess.run(list(map(str,command)),cwd=cwd,env=env,capture_output=True,text=True,timeout=300)
    (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');report['commands'].append({'label':label,'exit_code':r.returncode});print(label,r.returncode,(r.stdout+r.stderr)[-2200:],flush=True);r.check_returncode();return r.stdout
try:
    for label,root in roots:
        includes=[str(root/Path(f).relative_to(fp.REPO)) if f!='-I' else f for f in fp.test_include_flags()]
        extra=[]
        if label=='baseline':
            # Visibility only; baseline runtime code remains byte-for-byte unchanged.
            overlay=out/'baseline-access';overlay.mkdir()
            for name,marker in [('renderer.cpp','class Device final : public GuardedMirrorDevice {'),('world_renderer.h','class WorldRenderer {')]:
                (overlay/name).write_text((root/fp.src(name).relative_to(fp.REPO)).read_text(encoding='utf-8').replace(marker,marker+'\n    friend struct PipelineOffTest;',1),encoding='utf-8')
            includes=['-I',str(overlay),*includes];extra=['-DNORTHLIGHT_PIPELINE_BASELINE=1']
        exe=out/(label+'.exe')
        run('compile-'+label,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*extra,*includes,fp.TESTS/'pipeline_all_off_gpu.cpp',*(root/fp.src(s).relative_to(fp.REPO) for s in sources),'-luser32','-luuid','-o',exe],out)
    for repetition in range(2):
        for label,root in (roots if repetition%2==0 else roots[::-1]):
            settings=out/f'{label}-settings-{repetition}';settings.mkdir()
            (settings/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n',encoding='utf-8')
            output=run(f'{label}-{repetition}',[out/(label+'.exe'),backend,settings],settings)
            assert 'ALL_OFF_PIPELINE_PASS' in output and 'ALL_OFF_IMAGE_PASS' in output
            if label=='current':assert 'ALL_OFF_WORKER_PASS' in output
            modes=report['timings'].setdefault(label,{})
            for mode,ms in re.findall(r'ALL_OFF_CPU mode=(\d+) round=\d+ ms=(\S+)',output):modes.setdefault(mode,[]).append(float(ms))
            frame_modes=report['frame_timings'].setdefault(label,{})
            for mode,ms in re.findall(r'ALL_OFF_FRAME_CPU mode=(\d+) round=\d+ frames=200 ms=(\S+)',output):frame_modes.setdefault(mode,[]).append(float(ms))
    report['median_ms']={label:{mode:statistics.median(values) for mode,values in modes.items()} for label,modes in report['timings'].items()}
    report['frame_median_ms']={label:{mode:statistics.median(values) for mode,values in modes.items()} for label,modes in report['frame_timings'].items()}
    report['pass']=True
finally:
    guard.processes(allow_game=args.allow_running_game);report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report.get('median_ms',{})));assert report['pass']
