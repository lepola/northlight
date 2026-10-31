#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,world-cache manual timing
"""Real Telredor point pass: exact visible cubes and optional paired timings; no game."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend',type=Path,action='append',required=True)
p.add_argument('--benchmark',action='store_true')
a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('point_guard',fp.TESTS/'validate_native_scene_trace.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard);guard.processes()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for folder in [fp.SRC,fp.SHADERS] for path in folder.rglob('*') if path.is_file()}
inputs.update({str(path):sha(path) for path in [Path(__file__),fp.TESTS/'point_shadow_visibility_gpu.cpp',fp.TESTS/'scene_pressure_fixture.h']})
cache=fp.world_cache();cache_files={cache/'lights/Expansion01.fgl'}
for y in range(29,34):
    for x in range(18,23):
        tile=cache/'Expansion01'/f'{x}_{y}.fg3'
        if not tile.is_file():continue
        data=tile.read_bytes();assert (len(data)-12)%116==0 and struct.unpack_from('<4sII',data)==(b'FGS3',3,(len(data)-12)//116)
        cache_files.add(tile)
        for offset in range(12,len(data),116):cache_files.add(cache/'models'/(data[offset:offset+32].hex()+'.fgs'))
cache_inputs={str(path):{'sha256':sha(path),'bytes':path.stat().st_size} for path in sorted(cache_files)}
env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DXVK_LOG_PATH=str(out),DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DISABLE_VULKAN_OBS_CAPTURE='1')
exe=out/'point-visibility.exe'
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'point_shadow_visibility_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
report={'pass':False,'inputs':inputs,'cache_inputs':cache_inputs,'command':list(map(str,command)),'game_launched':False,'HDR':False,'benchmark':a.benchmark,'cases':[]}
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
for index,backend in enumerate(a.backend):
    guard.processes();run=out/str(index);run.mkdir();settings=run/'settings';settings.mkdir()
    (settings/'northlight-quality.ini').write_text('[Quality]\nPreset=Quality\n[Experimental]\nHDR=0\n')
    cmd=[str(exe),str(backend.resolve()),str(fp.world_cache()),str(settings)+os.sep,str(run)]+(['benchmark'] if a.benchmark else [])
    with (run/'gpu.log').open('w') as log:result=subprocess.run(cmd,cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
    text=(run/'gpu.log').read_text();print(text[-3500:],flush=True);assert result.returncode==0 and 'POINT_VISIBILITY_GPU_PASS' in text
    groups={}
    for line in (run/'timings.tsv').read_text().splitlines():
        limit,pose,optimized,block,submit,complete,updates=line.split()
        groups.setdefault((int(limit),int(pose),int(optimized)),[]).append((float(submit),float(complete),int(updates)))
    timing={str(key):{'submit_ms':statistics.median(r[0] for r in rows),'completed_ms':statistics.median(r[1] for r in rows),'cube_updates':statistics.median(r[2] for r in rows),'blocks':len(rows)} for key,rows in groups.items()}
    report['cases'].append({'backend':str(backend.resolve()),'backend_sha256':sha(backend),'summary':text.split('POINT_VISIBILITY_GPU_PASS')[-1].strip(),'timing':timing})
guard.processes();assert all(sha(Path(path))==digest for path,digest in inputs.items())
assert all(sha(Path(path))==record['sha256'] for path,record in cache_inputs.items())
report['pass']=True;report['limitation']='Static cache component workload and authored camera orbit; no native animated draws, whole-frame replay or FPS claim. Completion includes CPU submission and GPU execution.'
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',out)
