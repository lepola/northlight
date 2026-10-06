#!/usr/bin/env python3
# northlight-test: requires=cxx slow
"""Paged replay residency (replay_gpu_batches.h) against the per-mesh cache: frame-by-frame
bit-identical software raster from bound buffers, write-once page regions, committed-byte
accounting, faults, trims and reset; plus the production wiring. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(file,key):return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==key for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub').replace('struct IDirect3DDevice9{','''constexpr unsigned D3DSAMP_ADDRESSU=1,D3DSAMP_ADDRESSV=2;
struct IDirect3DVertexShader9{};struct IDirect3DBaseTexture9{};
struct IDirect3DDevice9{
virtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;
virtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;
virtual HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9*)=0;virtual HRESULT SetStreamSource(UINT,IDirect3DVertexBuffer9*,UINT,UINT)=0;
virtual HRESULT SetIndices(IDirect3DIndexBuffer9*)=0;virtual HRESULT SetVertexShader(IDirect3DVertexShader9*)=0;virtual HRESULT SetTexture(DWORD,IDirect3DBaseTexture9*)=0;
virtual HRESULT SetSamplerState(DWORD,DWORD,DWORD)=0;virtual HRESULT SetPixelShaderConstantF(UINT,const float*,UINT)=0;''')
world=fp.src('world_renderer.h').read_text()
batched=fp.src('replay_gpu_batches.h').read_text()
# Production wiring: the selected cache, the shared binding helper, and pool/usage identical to the per-mesh buffers.
assert 'inline constexpr bool BatchedReplayCache=true;' in batched
assert '    NorthlightReplayGPU::SelectedCache replayGpuCache;' in world
assert 'auto bindReplay=[&](Replay& p){NorthlightReplayGPU::bindResident(replayGpuCache,d,p,allowCache,admission);};' in world
assert batched.count('D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT')==2 and batched.count('D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT')==2
# Every buffer is written once, fresh, like the per-mesh buffers: NOOVERWRITE through upload_lock.h (a flags-0 lock drains DXVK's CS thread), never DISCARD.
cache=fp.src('replay_gpu_cache.h').read_text()
for text in (batched,cache):
    locks=re.findall(r'->Lock\(([^;]*?),&out,([^)]*)\)',text)
    assert locks and all(flags=='NorthlightUpload::FreshBufferLock' for _,flags in locks),locks
    assert 'D3DLOCK_DISCARD' not in text and 'D3DLOCK_NOOVERWRITE' not in text and text.count('->Lock(')==len(locks)
assert len(re.findall(r'->Lock\(',batched))==3 and len(re.findall(r'->Lock\(',cache))==2
assert '#include "upload_lock.h"' in cache and 'inline constexpr DWORD FreshBufferLock=NoOverwriteFreshBuffers?DWORD(D3DLOCK_NOOVERWRITE):DWORD(0);' in fp.src('upload_lock.h').read_text()
# Draws read BaseVertexIndex/StartIndex from the replay in both loops.
for f in ('world_renderer.h','world_point_rendering.inl'):
    assert 'p->indexed?d->DrawIndexedPrimitive(p->type,p->base,p->min,p->vertices,p->start,p->count):d->DrawPrimitive(p->type,p->start,p->count);' in fp.tracked(f).read_text()
print('PASS wiring: SelectedCache, bindResident, DEFAULT|WRITEONLY buffers, fresh buffers written once with FreshBufferLock, base/start in both replay loops')
with tempfile.TemporaryDirectory(prefix='northlight-replay-batches-') as tmp:
    p=Path(tmp);(p/'d3d9.h').write_text(stub)
    for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']):
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),*fp.test_include_flags(),str(HERE/'test_replay_gpu_batches.cpp'),'-o',str(p/'test')],check=True)
        print(' '.join(flags[:1])+(' asan+ubsan' if len(flags)>1 else ''),flush=True);subprocess.run([str(p/'test')],check=True)
