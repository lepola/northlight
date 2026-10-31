#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Combined material CPU correctness and allocation regressions; no game."""
import subprocess,sys,json,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
out=fp.output_dir()/'material-followup-cpu';out.mkdir(parents=True,exist_ok=True)
helper=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(){')[0]
helper=helper.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n static void textureStorage();')
reference=(fp.TESTS/'fixtures/material_height_access.inc').read_text()
helper=helper.replace('struct DisplacementStateTestAccess {','struct DisplacementStateTestAccess {\n'+reference+'\n static void borrowedHeight();')
(out/'material_followup_fixture.h').write_bytes(helper.encode())
runs=[]
for name in ['material_texture_pipeline','material_followup_cpu','fog_global_support']:
 exe=out/(name+'.exe');log=out/(name+'.compile.log')
 with log.open('w') as f:subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/(name+'.cpp')),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),stdout=f,stderr=subprocess.STDOUT,check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout,flush=True);assert r.returncode==0,r.stderr;runs.append(name)
(out/'validation.json').write_text(json.dumps({'pass':True,'tests':runs,'game_launched':False},indent=2)+'\n')
