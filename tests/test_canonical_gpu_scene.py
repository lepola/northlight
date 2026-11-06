#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Single canonical GPU scene versus the shipped peer caches, exact stock M2 bytes."""
import argparse,hashlib,json,re,statistics,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import replay_gpu_fixture
from client_fixtures import shared_gpu_models
p=argparse.ArgumentParser(__doc__);p.add_argument('--headers',type=Path);p.add_argument('--output',type=Path)
p.add_argument('--benchmark',action='store_true');p.add_argument('--pairs',type=int,default=7);a=p.parse_args()
out=(a.output or fp.output_dir()/'canonical-gpu-scene').resolve();out.mkdir(parents=True,exist_ok=True)
headers=(a.headers or fp.src('scene_native_store.h').parent).resolve();sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
stub=replay_gpu_fixture.stub;first=stub.index('using UINT');last=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:first]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[last:]
(out/'d3d9.h').write_text(stub);(out/'replay_fixture.h').write_text(replay_gpu_fixture.fixture)
fixture,sources,count=shared_gpu_models(out)
source=fp.TESTS/'test_canonical_gpu_scene.cpp';inputs={str(q):sha(q) for q in [Path(__file__),source,fp.TESTS/'test_shared_gpu_residency.cpp',headers/'scene_native_store.h']}
env=fp.zig_env()
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
runs=[];optimized=None
for label,flags in [('O2',['-O2']),('ASan-UBSan',['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 exe=out/(label+'.exe');command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(out),'-I',str(headers),*fp.test_include_flags(),str(source),'-o',str(exe)]
 subprocess.run(command,env=env,check=True)
 result=subprocess.run([str(exe),str(fixture)],env=env,text=True,capture_output=True,timeout=180);(out/(label+'.log')).write_text(result.stdout+result.stderr)
 print(result.stdout+result.stderr,flush=True);result.check_returncode();assert 'CANONICAL_SCENE_EXACT_PASS overlap=100' in result.stdout
 runs.append({'label':label,'command':command,'output':result.stdout})
 if label=='O2':optimized=exe
rows=[];summary={}
if a.benchmark:
 for overlap in (0,50,100):
  for pair in range(a.pairs+2):
   for mode in (('peer','canonical') if pair%2==0 else ('canonical','peer')):
    text=subprocess.check_output([str(optimized),str(fixture),mode,str(overlap)],env=env,text=True)
    row=dict(re.findall(r'(\w+)=([^\s]+)',text));row.update(mode=mode,pair=pair-2);rows.append(row)
  summary[str(overlap)]={mode:{key:statistics.median(float(r[key]) for r in rows if r['mode']==mode and r['pair']>=0 and r['overlap']==str(overlap)) for key in ('cpuMs','uploads','fallbackBytes','peak','creates')} for mode in ('peer','canonical')}
 assert len({r['digest'] for r in rows})==1
assert all(sha(q)==value for q,value in inputs.items()),'Inputs changed during tests'
(out/'validation.json').write_text(json.dumps({'pass':True,'sources':sources,'meshes':count,'fixture_sha256':sha(fixture),'inputs':inputs,'runs':runs,'rows':rows,'summary':summary,'game_launched':False,'GPU_used':False},indent=2)+'\n')
print(json.dumps(summary,indent=2))
