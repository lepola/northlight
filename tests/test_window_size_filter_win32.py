#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Real Win32 subclass/lifecycle and private-target redundant-resize regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--output',type=Path,default=fp.output_dir()/'window-size-filter-win32')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sources=['settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[fp.TESTS/'window_size_filter_win32.cpp',Path(__file__),*[fp.src(s) for s in sources],fp.src('settings_overlay.h'),fp.src('window_size_filter.h'),fp.src('hdr_compiled_shaders.h')]
report={'pass':False,'game_launched':False,'game_modified':False,'dummy_game_reset':True,'actual_device_reset':False,'private_d3d_shadow_marker':True,'foreground_query_fact_seam':True,'reentrant_lifecycle_test_seam':True,'real_win32_subclass_and_geometry':True,'enabled_overlay_ownership_cases':2,'hidden_windows':True,'timing_or_fps_claim':False,'inputs':{str(path.resolve().relative_to(fp.REPO)):sha(path) for path in inputs},'runs':[]}
report['reviewed_inputs']={'src/proxy/renderer.cpp':sha(fp.src('renderer.cpp'))}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
exe=out/'window-size-filter.exe'
def build(target,overlay):
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_WINDOW_SIZE_TEST_FOREGROUND','-DNORTHLIGHT_WINDOW_SIZE_TEST_LIFECYCLE','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.include_flags(),str(fp.TESTS/'window_size_filter_win32.cpp'),str(overlay),*[str(fp.src(s)) for s in sources[1:]],'-luser32','-luuid','-o',str(target)],env=fp.zig_env(),check=True)
build(exe,fp.src('settings_overlay.cpp'))
for name,backend in [('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    run=subprocess.run([str(exe),str(backend)],cwd=out,text=True,capture_output=True,timeout=90)
    (out/(name+'.log')).write_text(run.stdout+run.stderr)
    report['runs'].append({'backend':name,'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,run.stdout+run.stderr,end='',flush=True);run.check_returncode();assert 'WINDOW_SIZE_FILTER_WIN32_PASS' in run.stdout
# Reproduce the prior release-before-cleanup race in an artifact-local copy.
# Its first reentrant handover must violate the same candidate assertion.
overlay=fp.src('settings_overlay.cpp').read_text();claim='if(!hook.presentation.beginRelease(identity))return;'
assert overlay.count(claim)==1
negative_source=out/'settings_overlay-release-before-cleanup.cpp'
negative_source.write_text(overlay.replace(claim,'if(hook.presentation.owner()!=identity)return;hook.presentation.release(identity);'))
negative_exe=out/'window-size-filter-negative.exe';build(negative_exe,negative_source)
negative=subprocess.run([str(negative_exe),str(Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll')],cwd=out,text=True,capture_output=True,timeout=90)
expected='!NorthlightWindowHook::attach(closingReplacement,window)'
detected=negative.returncode!=0 and expected in negative.stdout+negative.stderr
report['negative_lifecycle']={'source_sha256':sha(negative_source),'exit_code':negative.returncode,'stdout':negative.stdout,'stderr':negative.stderr,'expected_assertion':expected,'detected':detected}
(out/'negative-lifecycle.log').write_text(negative.stdout+negative.stderr)
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert detected,report['negative_lifecycle']
print('WINDOW_SIZE_LIFECYCLE_NEGATIVE_PASS release_before_cleanup_detected=1',flush=True)
report['inputs_unchanged']=all(sha(fp.REPO/path)==digest for path,digest in {**report['inputs'],**report['reviewed_inputs']}.items())
report['pass']=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');assert report['pass']
