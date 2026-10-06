#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Fake D3D buffers: exercises real cache policy, no GPU/game execution."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(file,key):return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==key for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
stub=stub.replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
body=r'''
#include "replay_gpu_cache.h"
#include <cassert>
#include <cstdio>
static size_t alive=0;static unsigned freshLocks=0,plainLocks=0,overlaps=0; /* resident uploads; asserted by main only: fixture users run archived flags-0 caches */
template<class T,class D>struct Buffer:T{
 unsigned refs=1;std::vector<unsigned char> data;D desc;bool fail=false;bool dynamic=false;std::vector<std::pair<UINT,UINT>> written;
 Buffer(size_t n):data(n){desc.Size=UINT(n);alive+=n;}~Buffer(){alive-=data.size();}
 unsigned AddRef()override{return ++refs;}unsigned Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT GetDesc(D* d)override{*d=desc;return D3D_OK;}
 HRESULT Lock(UINT o,UINT n,void** p,DWORD flags)override{if(fail||o+n>data.size())return E_POINTER;
  // Resident buffers are fresh and written once: NOOVERWRITE (upload_lock.h), disjoint ranges. Test readbacks are READONLY.
  if(!dynamic&&flags!=D3DLOCK_READONLY){(flags==D3DLOCK_NOOVERWRITE?freshLocks:plainLocks)+=1;for(auto r:written)overlaps+=!(o+n<=r.first||o>=r.second);written.push_back({o,o+n});}
  *p=data.data()+o;return D3D_OK;}
 HRESULT Unlock()override{return D3D_OK;}
};
struct Device:IDirect3DDevice9{
 unsigned calls=0,failAt=0;bool lockFail=false;
 HRESULT CreateVertexBuffer(UINT n,DWORD usage,UINT,unsigned,IDirect3DVertexBuffer9** out,void*)override{if(++calls==failAt)return E_POINTER;auto p=new Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>(n);p->fail=lockFail;p->dynamic=usage&D3DUSAGE_DYNAMIC;*out=p;return D3D_OK;}
 HRESULT CreateIndexBuffer(UINT n,DWORD usage,D3DFORMAT,unsigned,IDirect3DIndexBuffer9** out,void*)override{if(++calls==failAt)return E_POINTER;auto p=new Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>(n);p->fail=lockFail;p->dynamic=usage&D3DUSAGE_DYNAMIC;*out=p;return D3D_OK;}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return E_POINTER;}HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)override{return E_POINTER;}HRESULT GetStreamSourceFreq(UINT,UINT*)override{return E_POINTER;}HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)override{return E_POINTER;}HRESULT GetIndices(IDirect3DIndexBuffer9**)override{return E_POINTER;}
};
std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh(unsigned bytes=240){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->streams[0].stride=24;m->streams[0].bytes.resize(bytes);m->streams[1].stride=8;m->streams[1].bytes.resize(80);m->indices={0,1,2};for(unsigned i=0;i<bytes;++i)m->streams[0].bytes[i]=i%251;return m;}
int main(){
 NorthlightReplayGPU::Cache cache;Device d;IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;
 auto release=[&](){for(auto& p:vb){if(p)p->Release();p=nullptr;}if(ib)ib->Release();ib=nullptr;};auto admit=[](size_t){return true;};auto m=mesh();
 cache.beginFrame();assert(!cache.bind(&d,m,vb,ib,admit)&&d.calls==0);cache.beginFrame();assert(cache.bind(&d,m,vb,ib,admit)&&cache.uploaded()==m->byteSize());unsigned warm=d.calls;
 void* p=nullptr;assert(vb[0]->Lock(0,240,&p,D3DLOCK_READONLY)==D3D_OK);assert(!memcmp(p,m->streams[0].bytes.data(),240));vb[0]->Unlock();auto* saved=vb[0];release();
 cache.beginFrame();assert(cache.bind(&d,m,vb,ib,admit)&&vb[0]==saved&&d.calls==warm&&cache.uploaded()==0&&cache.reused()==m->byteSize());release();
 auto changed=mesh();cache.beginFrame();assert(!cache.bind(&d,changed,vb,ib,admit));
 // Failed partial allocation releases every newly allocated buffer; previous cache remains usable.
 cache.beginFrame();size_t old=alive;d.failAt=d.calls+2;assert(!cache.bind(&d,changed,vb,ib,admit)&&alive==old&&vb[0]==nullptr);
 assert(cache.bind(&d,m,vb,ib,admit));release();d.failAt=0;
 cache.beginFrame();assert(!cache.bind(&d,changed,vb,ib,[](size_t){return false;})&&alive==old);assert(cache.bind(&d,changed,vb,ib,admit));release();
 m.reset();cache.beginFrame();assert(alive==changed->byteSize());changed.reset();cache.beginFrame();assert(cache.bytes()==0&&alive==0);
 // Large working set: bounded residency, no eviction of bindings used this frame.
 std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> many;
 for(unsigned i=0;i<80;++i)many.push_back(mesh(1024*1024));
 cache.beginFrame();for(auto& x:many){cache.bind(&d,x,vb,ib,admit);release();}
 for(unsigned frame=0;frame<30;++frame){cache.beginFrame();for(auto& x:many){cache.bind(&d,x,vb,ib,admit);release();}assert(cache.bytes()<=64u*1024u*1024u&&cache.uploaded()<=4u*1024u*1024u);}
 cache.clear();assert(alive==0);cache.beginFrame();assert(!cache.bind(&d,many[0],vb,ib,admit));
 cache.beginFrame();d.lockFail=true;assert(!cache.bind(&d,many[0],vb,ib,admit)&&alive==0);d.lockFail=false;
 assert(freshLocks>0&&!plainLocks&&!overlaps);
 puts("PASS GPU replay cache: byte-exact upload through NOOVERWRITE write-once locks, zero warm upload, changed snapshot isolation, failure cleanup/fallback, weak lifetime, reset, 64 MiB residency and 4 MiB/frame admission");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-replay-gpu-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(body)
 for flags in [[],['-fsanitize=address,undefined','-fno-omit-frame-pointer']]:
  subprocess.run(['clang++','-std=c++17','-O2',*flags,'-I'+str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
