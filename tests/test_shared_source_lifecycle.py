#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Shared immutable source identity, accounting and CPU/GPU lifetime proofs using fake D3D; no game/GPU."""
from pathlib import Path
import ast,subprocess,sys,hashlib,json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
def literal(name,key):return next(ast.literal_eval(n.value) for n in ast.parse((fp.TESTS/name).read_text(encoding='utf-8')).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==key for t in n.targets))
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
baseline={}
for source,namespace,target in [('src/world/draw_snapshot.h','NorthlightDrawSnapshot','legacy_draw_snapshot.h'),('src/replay/replay_gpu_cache.h','NorthlightReplayGPU','legacy_replay_gpu_cache.h'),('src/world/terrain_capture_bounds.h','NorthlightTerrainCapture','legacy_terrain_capture_bounds.h')]:
    data=(fp.TESTS/'fixtures/shared-source-before'/{'src/world/draw_snapshot.h': 'shared-before-draw.h', 'src/replay/replay_gpu_cache.h': 'shared-before-replay.h', 'src/world/terrain_capture_bounds.h': 'shared-before-terrain.h'}[source]).read_bytes();baseline[source]=hashlib.sha256(data).hexdigest();(out/target).write_text(data.decode().replace(namespace,namespace.replace('Northlight','Legacy')),encoding='utf-8')
print('SHARED_SOURCE_BASELINE',json.dumps(dict(ref='ac966ce',source_sha256=baseline)),flush=True)
stub=literal('test_terrain_snapshot.py','stub')
gpu_stub=stub.replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
(out/'snapshot_harness.h').write_text(literal('test_draw_snapshot.py','harness').split('int main()')[0],encoding='utf-8')
(out/'terrain_capture_harness.h').write_text(literal('test_terrain_snapshot.py','harness').split('int main()')[0],encoding='utf-8')
(out/'replay_gpu_harness.h').write_text(literal('test_replay_gpu_cache.py','body').split('int main()')[0],encoding='utf-8')
terrain=(fp.TESTS/'test_live_terrain_gpu.cpp').read_text(encoding='utf-8').split('void allocator()')[0].replace('struct Snapshot {','struct Snapshot {NorthlightSourceIdentity::Handle sourceIdentity;')
(out/'terrain_gpu_harness.h').write_text('#include "source_identity.h"\n'+terrain,encoding='utf-8')
for source in ['shared_source_lifecycle.cpp','shared_replay_identity.cpp','shared_terrain_identity.cpp','shared_source_frozen_diff.cpp','shared_terrain_capture_lifetime.cpp']:
    (out/'d3d9.h').write_text(gpu_stub if source=='shared_replay_identity.cpp' else stub,encoding='utf-8')
    for opt in ['-O2','-O0']:
        exe=out/(Path(source).stem+opt+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-DNL_STUB','-std=c++17','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/source),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
print('SHARED_SOURCE_LIFECYCLE_ALL_PASS x86 O2 O0',flush=True)
