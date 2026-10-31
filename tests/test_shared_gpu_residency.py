#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Shared material/shadow residency against separate caches on stock M2 bytes; no GPU/game."""
import argparse,hashlib,json,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import replay_gpu_fixture

p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path)
p.add_argument('--benchmark',action='store_true');p.add_argument('--pairs',type=int,default=7);a=p.parse_args()
assert a.pairs>0
out=(a.output or fp.output_dir()/'shared-gpu-residency').resolve();out.mkdir(parents=True,exist_ok=True)
headers=(a.headers or fp.src('replay_gpu_batches.h').parent).resolve()
sha=lambda b:hashlib.sha256(b).hexdigest()
stub=replay_gpu_fixture.stub
first=stub.index('using UINT');last=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:first]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[last:]
(out/'d3d9.h').write_text(stub)
(out/'replay_fixture.h').write_text(replay_gpu_fixture.fixture)
from client_fixtures import shared_gpu_models
path,sources,mesh_count=shared_gpu_models(out)
fixture=path.read_bytes()
inputs={str(q):sha(q.read_bytes()) for q in [Path(__file__),fp.TESTS/'client_fixtures.py',fp.TESTS/'test_shared_gpu_residency.cpp',headers/'replay_gpu_cache.h',headers/'replay_gpu_batches.h',headers/'replay_gpu_binding.h']}
snapshot=headers/'draw_snapshot.h' if (headers/'draw_snapshot.h').is_file() else fp.src('draw_snapshot.h')
inputs[str(snapshot)]=sha(snapshot.read_bytes())
runs=[];exe=None
environment=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global')]:
 (out/name).mkdir(exist_ok=True);environment[key]=str(out/name)
for label,flags in [('O2',['-O2']),('ASan-UBSan',['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 target=out/(label+'.exe')
 command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(out),'-I',str(headers),*fp.test_include_flags(),str(fp.TESTS/'test_shared_gpu_residency.cpp'),'-o',str(target)]
 subprocess.run(command,env=environment,check=True)
 result=subprocess.run([str(target),str(path)],env=environment,text=True,capture_output=True,timeout=180)
 (out/(label+'.log')).write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);result.check_returncode()
 assert 'SHARED_GPU_EXACT_PASS' in result.stdout and 'SHARED_GPU_CONTROLS_PASS' in result.stdout
 runs.append({'label':label,'command':command,'stdout':result.stdout,'exe_sha256':sha(target.read_bytes())})
 if label=='O2':exe=target
rows=[];summary={}
if a.benchmark:
 for overlap in (0,50,100):
  for pair in range(a.pairs+2):
   for mode in (('separate','shared') if pair%2==0 else ('shared','separate')):
    text=subprocess.check_output([str(exe),str(path),mode,str(overlap)],env=environment,text=True)
    row=dict(re.findall(r'(\w+)=([^\s]+)',text));row.update(pair=pair-2,mode=mode,overlap=overlap);rows.append(row)
  summary[str(overlap)]={mode:{key:statistics.median(float(r[key]) for r in rows if r['mode']==mode and r['pair']>=0 and r['overlap']==overlap) for key in ('cpuMs','uploads','fallbackBytes','peak','creates')} for mode in ('separate','shared')}
 assert len({r['digest'] for r in rows})==1
assert all(sha(Path(q).read_bytes())==v for q,v in inputs.items())
report={'pass':True,'headers':str(headers),'fixture_sha256':sha(fixture),'meshes':mesh_count,'sources':sources,'inputs':inputs,'runs':runs,'benchmark':a.benchmark,'rows':rows,'summary':summary,'game_launched':False,'GPU_used':False,'limits_MiB':{'world':64,'material':32},'limitation':'CPU and fake GPU resources; native timing/image acceptance is separate.'}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'meshes':mesh_count,'fixture_sha256':sha(fixture),'sources':sources,'timing':summary},indent=2))
