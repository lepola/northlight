#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""4096-entry frozen count-pressure differential and exact fake-D3D controls."""
from pathlib import Path
import ast,hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
def literal(name,key):
    return next(ast.literal_eval(n.value) for n in ast.parse((fp.TESTS/name).read_text(encoding='utf-8')).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==key for t in n.targets))
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
stub=literal('test_terrain_snapshot.py','stub').replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
(out/'d3d9.h').write_text(stub,encoding='utf-8')
(out/'replay_gpu_harness.h').write_text(literal('test_replay_gpu_cache.py','body').split('int main()')[0],encoding='utf-8')
baseline=subprocess.check_output(['git','show','cc69996:src/replay/replay_gpu_cache.h'])
(out/'legacy_replay_gpu_cache.h').write_text(baseline.decode().replace('NorthlightReplayGPU','LegacyReplayGPU'),encoding='utf-8')
print('REPLAY_PROBATION_BASELINE',hashlib.sha256(baseline).hexdigest(),flush=True)
runs=[]
for source in ['replay_gpu_probation.cpp','shared_replay_identity.cpp']:
    ref='cc69996' if source=='replay_gpu_probation.cpp' else 'ac966ce'
    frozen=subprocess.check_output(['git','show',ref+':src/replay/replay_gpu_cache.h'])
    (out/'legacy_replay_gpu_cache.h').write_text(frozen.decode().replace('NorthlightReplayGPU','LegacyReplayGPU'),encoding='utf-8')
    for opt in ['-O2','-O0']:
        exe=out/(Path(source).stem+opt+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/source),'-o',str(exe)],env=fp.zig_env(),check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True);print(source,opt,result.stdout,result.stderr,flush=True);result.check_returncode();runs.append(dict(source=source,opt=opt,stdout=result.stdout))
(out/'replay-probation-validation.json').write_text(json.dumps(dict(baseline_ref='cc69996',all_pass=True,game_launched=False,runs=runs),indent=2)+'\n',encoding='utf-8')
