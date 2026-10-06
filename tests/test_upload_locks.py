#!/usr/bin/env python3
# northlight-test:
"""0.3.151 buffer Lock audit (source only, no game, Wine or GPU). Every IDirect3D*Buffer9 Lock in
the renderer sources is listed below with the creation that decides its DXVK map mode. A flags-0
lock of a DEFAULT|WRITEONLY buffer is a DIRECT mapping without a sequence number: it drains the
CS thread (upload_lock.h). Such buffers are fresh and write-once and lock with
NorthlightUpload::FreshBufferLock; a new DEFAULT buffer or Lock fails here until it is classified."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re
HERE=Path(__file__).resolve().parent
F='NorthlightUpload::FreshBufferLock'
def arguments(text,open_at):
    depth=0;out=[];cur=''
    for ch in text[open_at:]:
        if ch=='(':
            depth+=1
            if depth==1:continue
        elif ch==')':
            depth-=1
            if depth==0:out.append(cur.strip());return out
        elif ch==',' and depth==1:out.append(cur.strip());cur='';continue
        cur+=ch
    raise AssertionError('unbalanced call')
sources={f.name:f.read_text(errors='replace') for f in fp.sources(('.h','.inl','.cpp'))}
# (file, receiver) -> (flags, class, creation evidence in the same file or None)
Fresh,Managed,Dynamic,Read,Forward='fresh DEFAULT|WRITEONLY','MANAGED','DEFAULT|DYNAMIC','READONLY','game flags'
expected={
 ('replay_gpu_batches.h','b'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s]'),
 ('replay_gpu_batches.h','batch->vertices'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vertices'),
 ('replay_gpu_batches.h','batch->indices'):(F,Fresh,'D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->indices'),
 ('replay_gpu_cache.h','built->vertices[s]'):(F,Fresh,'D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s]'),
 ('replay_gpu_cache.h','built->indices'):(F,Fresh,'D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built->indices'),
 ('static_shadow_gpu.h','r.vb'):('0',Managed,'D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&r->vb'),
 ('static_shadow_gpu.h','r.ib'):('0',Managed,'D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_MANAGED,&r->ib'),
 ('world_renderer.h','page.vb'):('0',Managed,'D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&slot.vb'),
 ('world_renderer.h','page.ib'):('0',Managed,'D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&slot.ib'),
 ('live_terrain_gpu.h','vertices_'):('discard_?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_'),
 ('static_shadow_gpu.h','instances_'):('flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances_'),
 ('world_renderer.h','liveIndicesGPU'):('D3DLOCK_DISCARD',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&liveIndicesGPU'),
 ('world_renderer.h','replayVerticesGPU[s]'):('D3DLOCK_DISCARD',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&replayVerticesGPU[s]'),
 ('world_renderer.h','replayIndicesGPU'):('D3DLOCK_DISCARD',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&replayIndicesGPU'),
 ('draw_snapshot.h','ib'):('D3DLOCK_READONLY',Read,None),('draw_snapshot.h','vertices[s]'):('D3DLOCK_READONLY',Read,None),
 ('draw_snapshot.h','vb'):('D3DLOCK_READONLY',Read,None),
 ('geometry_capture.h','indices.value'):('D3DLOCK_READONLY',Read,None),('geometry_capture.h','vertices.value'):('D3DLOCK_READONLY',Read,None),
 ('terrain_capture_bounds.h','ib.p'):('D3DLOCK_READONLY',Read,None),('terrain_capture_bounds.h','vb.p'):('D3DLOCK_READONLY',Read,None),
 ('forwarders.h','real'):('Flags',Forward,None),('tracked_buffers.h','this->real'):('flags',Forward,None),
}
seen=set();fresh=calls=0
for name,text in sources.items():
    for m in re.finditer(r'([A-Za-z_][\w.\->\[\]]*?)(?:->|\.)Lock\(',text):
        line=text.count('\n',0,m.start())+1;flags=arguments(text,m.end()-1)[-1];key=(name,m.group(1))
        assert key in expected,f'{name}:{line}: unclassified buffer Lock of {key[1]} with {flags}: add it to test_upload_locks.py'
        want,kind,created=expected[key];seen.add(key)
        assert flags==want,f'{name}:{line}: {key[1]} ({kind}) locks with {flags}, expected {want}'
        assert created is None or created in text,f'{name}:{line}: {key[1]} is no longer created as {kind}: {created}'
        fresh+=flags==F;calls+=1
assert seen==set(expected),f'stale entries: {sorted(set(expected)-seen)}'
assert fresh==5
print(f'PASS lock audit: {calls} buffer Lock calls in {len(expected)} classified sites, {fresh} fresh DEFAULT locks use {F}')
# Every DEFAULT buffer without DYNAMIC is one of the fresh write-once sites above.
creates=[]
for name,text in sources.items():
    for m in re.finditer(r'(?:->|\.)(Create(?:Vertex|Index)Buffer)\(',text):
        a=arguments(text,m.end()-1)
        if len(a)!=6 or a[0]in('size','Length','v[0]'):continue # the device wrappers and the stream's replay forward the game's own arguments
        pool,usage=a[3],a[1]
        if 'D3DPOOL_DEFAULT' in pool and 'D3DUSAGE_DYNAMIC' not in usage:creates.append((name,a[4]))
        else:assert 'D3DPOOL_MANAGED' in pool or 'D3DUSAGE_DYNAMIC' in usage,(name,a)
assert sorted(creates)==sorted([('replay_gpu_batches.h','&built->vertices[s]'),('replay_gpu_batches.h','&built->indices'),('replay_gpu_batches.h','&batch->vertices'),
    ('replay_gpu_batches.h','&batch->indices'),('replay_gpu_cache.h','&built->vertices[s]'),('replay_gpu_cache.h','&built->indices')]),creates
print(f'PASS DEFAULT|WRITEONLY creations: {len(creates)}, all fresh write-once sites')
# The constant and its kill switch; FreshBufferLock appears nowhere else.
upload=fp.src('upload_lock.h').read_text()
assert 'inline constexpr bool NoOverwriteFreshBuffers=true;' in upload
assert 'inline constexpr DWORD FreshBufferLock=NoOverwriteFreshBuffers?DWORD(D3DLOCK_NOOVERWRITE):DWORD(0);' in upload
assert {n for n,t in sources.items() if 'FreshBufferLock' in t}=={'upload_lock.h','replay_gpu_batches.h','replay_gpu_cache.h'}
assert '#include "upload_lock.h"' in sources['replay_gpu_cache.h'] # world_renderer.h (and its .inl) include replay_gpu_cache.h
# Publication strictly after the last Unlock of each fresh buffer; nothing else reaches an unpublished one.
def body(text,start,end):i=text.index(start);return text[i:text.index(end,i)]
commit=body(sources['replay_gpu_batches.h'],'    void commit(){','    void compact(){')
assert commit.count('->Lock(')==2 and commit.rindex('->Unlock()')<commit.index('batches_.push_back(std::move(batch));')<commit.index('e.batch=b;')
separate=body(sources['replay_gpu_batches.h'],'    bool storeSeparate(','    // The reserved admissions')
assert separate.rindex('write(')<separate.index('std::swap(e.vertices[s],built->vertices[s]);')
cache=body(sources['replay_gpu_cache.h'],'auto built=std::make_unique<Entry>();','}else{reused_+=e.bytes;++hits_;}')
assert cache.count('->Lock(')==2 and cache.rindex('->Unlock()')<cache.index('std::swap(e.vertices[s],built->vertices[s]);')
print('PASS fresh buffers: replay batches/meshes are published only after their last Unlock and never re-locked')
