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
 ('live_terrain_gpu.h','vertices_'):('discarding?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_'),
 ('static_shadow_gpu.h','instances_'):('flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances_'),
 ('world_renderer.h','liveIndicesGPU'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&liveIndicesGPU'),
 ('world_renderer.h','replayVerticesGPU[s]'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&replayVerticesGPU[s]'),
 ('world_renderer.h','replayIndicesGPU'):('slot.flags',Dynamic,'D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&replayIndicesGPU'),
 ('replay_copy_reader.h','b'):('NorthlightUpload::readBackLock()',Read,None), # 0.3.192 (CS): the 8 capture read-backs go through NorthlightReplayCopies::Reader (CPU copy first, this lock otherwise)
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
# 0.3.192 (DXVK3): every READONLY buffer read-back goes through readBackLock(); the definition, the gate and the absence of bare flags.
assert 'inline std::atomic<bool> ReadBackNoOverwrite{false};' in upload
assert 'inline DWORD readBackLock(){return DWORD(D3DLOCK_READONLY)|(ReadBackNoOverwrite.load(std::memory_order_relaxed)?DWORD(D3DLOCK_NOOVERWRITE):0u);}' in upload
assert upload.count('readBackLock()')>=1 and upload.count('D3DLOCK_READONLY)|')==1 # READONLY is always kept: tracked_buffers.h keys the revision on it
world_dir=fp.SRC/'world'
bare=[(f.name,text.count('\n',0,m.start())+1) for f in sorted(world_dir.iterdir()) if f.suffix in('.h','.inl','.cpp')
      for text in [f.read_text(errors='replace')] for m in re.finditer(r'(?:->|\.)Lock\(',text) if 'D3DLOCK_READONLY' in arguments(text,m.end()-1)[-1]]
assert not bare,f'bare D3DLOCK_READONLY buffer Lock in src/world (use NorthlightUpload::readBackLock()): {bare}'
sites=('draw_snapshot.h','geometry_capture.h','terrain_capture_bounds.h')
assert sum(sources[n].count('NorthlightUpload::readBackLock()') for n in sites)==0 # no capture site locks a game buffer itself any more
readers=sum(sources[n].count('NorthlightReplayCopies::Reader<') for n in sites)
assert readers==8,readers # 4 + 2 + 2 read-back sites, every one through the reader (replay_copy_reader.h)
reader=sources['replay_copy_reader.h']
assert reader.count('NorthlightUpload::readBackLock()')==2 and reader.count('->Lock(')==2 # the whole-buffer fill and the ordinary fallback lock
assert reader.index('if(enabled.load(std::memory_order_relaxed)&&b){')<reader.index('->Lock(0,slot->size') # the stream gate comes first: with it off only the fallback runs
renderer=sources['renderer.cpp']
gate='NorthlightUpload::ReadBackNoOverwrite.store(module&&!result.fallback&&last.info.dxvk&&NorthlightBackend::dxvkMajor(last.info.dxvkVersion)>=3,std::memory_order_relaxed);'
stores=[n for n,t in sources.items() if re.search(r'ReadBackNoOverwrite(?:\.store\(|\s*=[^=])',t.replace('inline std::atomic<bool> ReadBackNoOverwrite{false};',''))]
assert stores==['renderer.cpp'] and renderer.count('ReadBackNoOverwrite.store(')==1 and renderer.count(gate)==1,stores
# The gate runs after the BACKEND selected= log (the loaded module decides) and before CreateDevice can run.
assert renderer.index('BACKEND selected=')<renderer.index(gate)<renderer.index('readBackLock=0x%x')
print('PASS read-back lock: readBackLock() defined once, 8 read-back sites, no bare READONLY buffer Lock in src/world, gate stored once from the loaded DXVK >= 3 backend')
# 0.3.192 (CS): the replay-side CPU copies (replay_copies.h): one choke point (the tracked wrapper), gated on the stream, pressure-shrunk once per frame.
tracked=sources['tracked_buffers.h'];copies=sources['replay_copies.h']
assert tracked.count('NorthlightReplayCopies::writeLocked(')==1 and tracked.count('NorthlightReplayCopies::beforeUnlock(')==1 # the only two write hooks: Lock records the range, Unlock copies it out of the mapped pointer
assert tracked.index('NorthlightReplayCopies::beforeUnlock(')<tracked.index('this->real->Unlock()') # before the pointer is released
assert tracked.count('NorthlightReplayCopies::invalidate(it->second->copy,true)')==1 and 'invalidate(entry.second->copy)' in tracked # ProcessVertices (written) and Reset (invalidateAll)
assert tracked.count('NorthlightReplayCopies::enabled.load(')>=3 # attach, Lock and Unlock are all behind the stream gate
assert renderer.count('NorthlightReplayCopies::enabled.store(true')==1 and renderer.index('NorthlightReplayCopies::enabled.store(true')<renderer.index('real->CreateDevice(adapter,type,window,flags,pp,out)')
assert 'if(stream)NorthlightReplayCopies::enabled.store(true' in renderer and 'NorthlightReplayCopies::setPressure(memoryCaps==1)' in renderer
assert [n for n,t in sources.items() if 'enabled.store(true' in t and 'NorthlightReplayCopies' in t]==['renderer.cpp']
# every write a stream replays into a game buffer is a Lock/Unlock on the stream's inner object, which is the wrapper (Device::Create*Buffer wraps)
replay=sources['replay_thread.h']
assert 'static_cast<IDirect3DVertexBuffer9*>(p->inner)->Lock(off,size,&dst,flags)' in replay and replay.count('->Lock(off,size,&dst,flags)')==2 and 'writeBuffer(a->proxy,a->off,a->size,a->flags,a->src+a->off)' in replay and 'static_cast<IDirect3DVertexBuffer9*>(p->inner)->Unlock()' in replay
dev=renderer[renderer.index('class Device final'):renderer.index('class Factory final')]
assert 'ext->CreateVertexBuffer(' in dev and 'NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9' in dev and 'NorthlightTrackedBuffers::wrap<IDirect3DIndexBuffer9' in dev
assert dev.count('NorthlightTrackedBuffers::unwrap(buffer)')==1 # ProcessVertices is the only unwrapping use of a game buffer for a write
# R1: the capture holds the RAW buffer (ext->GetStreamSource/GetIndices after Device::SetStreamSource/SetIndices unwrap): the copy registry must resolve raw pointers too
assert 'attach(record.copy,record.raw,record.exposed,this,' in tracked and 's.registry[raw]=&c;s.registry[exposed]=&c;' in copies and 's.registry.erase(c.raw);s.registry.erase(c.exposed)' in copies
assert 'static_cast<T*>(this),record.metadata.size' not in tracked # not keyed by the wrapper pointer only
# R2: the thrash guard and the deferred fill are measured in frames, advanced once per frame on the replay thread (WorldRenderer::endFrame), never in reads
assert 'kThrashReads' not in copies and 'kBackoffReads' not in copies and 's.tick' not in copies and 'c.frames>=2' in copies and 'c.frames=0;' in copies
assert sources['world_renderer.h'].count('NorthlightReplayCopies::advanceFrame()')==1
# R3: a pin holds a COM reference on the wrapper that owns the slot; the last Release happens outside the store mutex
assert 'c.ref(c.owner,true)' in copies and copies.count('ref(owner,false)')==2
print('PASS replay copies wiring: gate set before the first buffer, wrapper hooks, ProcessVertices/Reset invalidation, pressure shrink')
