#!/usr/bin/env python3
# northlight-test: requires=cxx slow
"""Low address-space guard. Native tests of the production memory_guard.h state
machine (thresholds, hysteresis, cooldown, tick wrap, cadence, after-trim sample);
the three production caches it trims or caps (model snapshot +
index cache, terrain content cache, MODEL GPU cache) with fake D3D buffers:
consistent after trim/cap, shared holders stay valid, lookups rebuild identical
bytes; plus a source audit of the render-thread wiring. No Wine, Windows binary
or game is run. -O2 and ASan/UBSan; the sampler/guard handoff also under TSan.
BENCH lines: main-thread trim cost on production-like cache sizes (native macOS,
fake D3D; DXVK buffer Release cost not modelled)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(file,key):return next(ast.literal_eval(n.value) for n in ast.parse(fp.tracked(file).read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==key for t in n.targets))
stub=literal('test_terrain_snapshot.py','stub')

guard=r'''
#include "memory_guard.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace NorthlightMemoryGuard;
int main(){
    // Thresholds: either largest or aggregate below the enter level starts pressure and trims at once.
    {Guard g;auto d=g.update(3000*MiB,2000*MiB,1000);assert(!d.trim&&!d.entered&&!g.pressure()&&d.report);
     d=g.update(3000*MiB,160*MiB,2000);assert(!d.trim&&!g.pressure()); // exactly at the threshold: not low
     d=g.update(3000*MiB,159*MiB,3000);assert(d.trim&&d.entered&&g.pressure()&&g.trims()==1&&g.entries()==1&&d.report);
     assert(g.minLargest()==159*MiB&&g.minAvailable()==3000*MiB);}
    {Guard g;auto d=g.update(399*MiB,1500*MiB,5);assert(d.trim&&d.entered&&g.pressure());}
    // Cooldown: still low, no second trim before 20 s; then one per cooldown.
    {Guard g;assert(g.update(1000*MiB,100*MiB,0).trim);
     for(std::uint32_t t=1000;t<20000;t+=1000){auto d=g.update(1000*MiB,90*MiB,t);assert(!d.trim&&!d.entered&&g.pressure());}
     assert(g.update(1000*MiB,90*MiB,20000).trim&&g.trims()==2);
     assert(!g.update(1000*MiB,90*MiB,39999).trim);assert(g.update(1000*MiB,90*MiB,40000).trim&&g.trims()==3&&g.entries()==1);}
    // Hysteresis: between thresholds keeps pressure; recovery needs 10 s of consecutive healthy samples.
    {Guard g;g.update(1000*MiB,100*MiB,0);
     for(std::uint32_t t=1000;t<60000;t+=1000)assert(!g.update(1000*MiB,200*MiB,t).recovered&&g.pressure()); // 160..256: stay
     assert(!g.update(1000*MiB,300*MiB,60000).recovered);assert(!g.update(1000*MiB,300*MiB,65000).recovered);
     assert(!g.update(1000*MiB,250*MiB,66000).recovered); // dip below recovery: hold restarts
     assert(!g.update(1000*MiB,300*MiB,67000).recovered);assert(!g.update(1000*MiB,300*MiB,76999).recovered);
     auto d=g.update(1000*MiB,300*MiB,77000);assert(d.recovered&&!g.pressure()&&d.report);
     assert(!g.update(600*MiB,300*MiB,78000).entered); // healthy largest, 400..640 available: normal state kept
     assert(!g.update(1000*MiB,170*MiB,79000).entered&&!g.pressure());
     d=g.update(1000*MiB,150*MiB,80000);assert(d.entered&&d.trim&&g.entries()==2&&g.trims()==2);} // cooldown long over: trims again
    // Re-entry inside the trim cooldown enters pressure (caps) without another trim.
    {Guard g;assert(g.update(1000*MiB,100*MiB,0).trim);g.update(1000*MiB,300*MiB,1000);assert(g.update(1000*MiB,300*MiB,11000).recovered);
     auto d=g.update(1000*MiB,150*MiB,12000);assert(d.entered&&!d.trim&&g.pressure()&&g.entries()==2&&g.trims()==1);
     assert(g.update(1000*MiB,150*MiB,20000).trim&&g.trims()==2);}
    // Available must also recover.
    {Guard g;g.update(300*MiB,1000*MiB,0);for(std::uint32_t t=0;t<=30000;t+=1000)assert(!g.update(639*MiB,1000*MiB,1000+t).recovered);
     g.update(640*MiB,1000*MiB,40000);assert(g.update(640*MiB,1000*MiB,50000).recovered);}
    // GetTickCount wrap: cooldown and recovery arithmetic stay correct across 2^32 ms.
    {Guard g;const std::uint32_t t0=0xFFFFF000u;assert(g.update(1000*MiB,100*MiB,t0).trim);
     assert(!g.update(1000*MiB,100*MiB,t0+10000).trim);assert(g.update(1000*MiB,100*MiB,t0+20000).trim);
     g.update(1000*MiB,300*MiB,t0+21000);assert(g.update(1000*MiB,300*MiB,t0+31000).recovered);}
    // Walk cadence: first request immediately, 3 s normally, 1 s under pressure; wrap-safe.
    {Guard g;assert(g.requestDue(100));assert(!g.requestDue(2000));assert(g.requestDue(3100));
     g.update(1000*MiB,100*MiB,3200);assert(!g.requestDue(4000));assert(g.requestDue(4100));
     Guard w;assert(w.requestDue(0xFFFFFF00u));assert(!w.requestDue(0x00000100u));assert(w.requestDue(0x00000C00u));}
    // After-trim sample: only a walk that finished after the completed trim reports it, once.
    {Guard g;assert(g.update(1000*MiB,100*MiB,1000).trim);assert(!g.update(1000*MiB,100*MiB,1010).afterTrim);
     g.trimCompleted(1020);assert(!g.update(1000*MiB,100*MiB,1020).afterTrim); // same tick: may predate the trim
     assert(g.update(1000*MiB,400*MiB,1040).afterTrim);assert(!g.update(1000*MiB,400*MiB,2040).afterTrim);}
    // Periodic report cadence (10 s) plus every event.
    {Guard g;assert(g.update(3000*MiB,2000*MiB,0).report);assert(!g.update(3000*MiB,2000*MiB,3000).report);
     assert(g.update(3000*MiB,2000*MiB,10000).report);assert(g.update(3000*MiB,100*MiB,11000).report);}
    assert(Guard{}.minAvailable()==0&&Guard{}.minLargest()==0);
    std::puts("PASS memory guard: enter on largest<160 or available<400 MiB, 20 s trim cooldown, recovery only after 10 s at >=256/640 MiB, hysteresis band, re-entry within cooldown caps without trimming, tick wrap, 3 s/1 s walk cadence, after-trim sample, report cadence");
}
'''

snapshot=literal('test_draw_snapshot.py','harness').split('int main()')[0]+r'''
#include <chrono>
#include <map>
static std::map<void*,std::uint64_t> tokens;
static std::uint64_t identity(void* p,bool){auto it=tokens.find(p);return it==tokens.end()?0:it->second;}
static std::uint64_t digest(const Mesh& m){std::uint64_t h=0xcbf29ce484222325ull;auto mix=[&](unsigned char v){h^=v;h*=0x100000001b3ull;};
 for(auto& s:m.streams)for(auto b:s.bytes)mix(b);for(auto i:m.indices)for(unsigned j=0;j<4;++j)mix((i>>(j*8))&255);mix(m.vertexCount&255);return h;}
int main(){
 Device d;for(unsigned s=0;s<2;++s)d.vb[s].desc.Usage=D3DUSAGE_WRITEONLY;tokens[&d.vb[0]]=1;tokens[&d.vb[1]]=2;tokens[&d.ib]=3;
 constexpr unsigned vertices=4096,n=3*8000;d.vb[0].bytes.resize(d.offset[0]+vertices*d.stride[0]);d.vb[1].bytes.resize(d.offset[1]+vertices*d.stride[1]);
 for(unsigned s=0;s<2;++s)for(unsigned i=0;i<d.vb[s].bytes.size();++i)d.vb[s].bytes[i]=std::uint8_t(i*31+s);
 d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.resize(n*2);for(unsigned i=0;i<n;++i){std::uint16_t x=std::uint16_t(i%vertices);std::memcpy(d.ib.bytes.data()+i*2,&x,2);}
 d.ib.desc.Usage=0;
 Frame frame;frame.setIdentityProvider(&identity);Mesh mesh;Diagnostics why;
 Draw big{D3DPT_TRIANGLELIST,0,0,vertices,0,n/3,true};
 auto variant=[&](unsigned i){Draw v=big;v.primitives=n/3-i;return v;};
 auto fill=[&](unsigned count){for(unsigned i=0;i<count;++i){frame.clearFrame();std::shared_ptr<const Mesh> m;assert(frame.read(&d,&d.decl,variant(i),mesh,&why,false,&m)&&m);}};
 assert(frame.snapshotCacheLimit()==Frame::SnapshotCacheLimit);
 fill(400);const size_t full=frame.snapshotCacheBytes();assert(full>Frame::SnapshotCacheLimit/2&&full<=Frame::SnapshotCacheLimit);
 // A holder of a cached mesh (a replay or the GPU cache owner) survives any trim.
 std::shared_ptr<const Mesh> held;frame.clearFrame();assert(frame.read(&d,&d.decl,variant(399),mesh,&why,false,&held)&&frame.snapshotCacheHits()==1);
 const auto heldDigest=digest(*held);
 // Pressure cap: evicts oldest down to half now and stays there while filling.
 frame.setSnapshotCacheLimit(Frame::SnapshotCacheLimit/2);assert(frame.snapshotCacheLimit()==Frame::SnapshotCacheLimit/2&&frame.snapshotCacheBytes()<=Frame::SnapshotCacheLimit/2);
 fill(400);assert(frame.snapshotCacheBytes()<=Frame::SnapshotCacheLimit/2);assert(digest(*held)==heldDigest);
 frame.setSnapshotCacheLimit(size_t(1)<<40);assert(frame.snapshotCacheLimit()==Frame::SnapshotCacheLimit); // never above the static cap
 // Trim: index + snapshot caches empty, then the same draw rebuilds byte-identical content (miss, then hit).
 frame.clearIndexCache();assert(frame.snapshotCacheEntries()==0&&frame.snapshotCacheBytes()==0&&frame.indexCacheEntries()==0);
 assert(digest(*held)==heldDigest&&held.use_count()==1);
 std::shared_ptr<const Mesh> rebuilt;frame.clearFrame();assert(frame.read(&d,&d.decl,variant(399),mesh,&why,false,&rebuilt)&&frame.snapshotCacheMisses()==1);
 assert(rebuilt!=held&&digest(*rebuilt)==heldDigest);
 std::shared_ptr<const Mesh> again;frame.clearFrame();assert(frame.read(&d,&d.decl,variant(399),mesh,&why,false,&again)&&frame.snapshotCacheHits()==1&&again==rebuilt);
 Mesh plain;frame.clearFrame();assert(frame.read(&d,&d.decl,variant(399),plain,&why)&&digest(plain)==heldDigest);
 // Recovery restores the full cap.
 frame.setSnapshotCacheLimit(Frame::SnapshotCacheLimit);fill(400);assert(frame.snapshotCacheBytes()>Frame::SnapshotCacheLimit/2&&frame.snapshotCacheBytes()<=Frame::SnapshotCacheLimit);
 {// Main-thread trim cost at production-like size (log: 2231 entries, 64 MiB; index cache 1024 plans).
  Device b;for(unsigned s=0;s<2;++s)b.vb[s].desc.Usage=D3DUSAGE_WRITEONLY;tokens[&b.vb[0]]=11;tokens[&b.vb[1]]=12;tokens[&b.ib]=13;
  constexpr unsigned bv=1024,bn=3*900;b.vb[0].bytes.resize(b.offset[0]+bv*b.stride[0]);b.vb[1].bytes.resize(b.offset[1]+bv*b.stride[1]);
  for(unsigned s=0;s<2;++s)for(unsigned i=0;i<b.vb[s].bytes.size();++i)b.vb[s].bytes[i]=std::uint8_t(i*13+s);
  b.ib.desc.Format=D3DFMT_INDEX16;b.ib.desc.Usage=0;b.ib.bytes.resize(bn*2);for(unsigned i=0;i<bn;++i){std::uint16_t x=std::uint16_t((i*7)%640);std::memcpy(b.ib.bytes.data()+i*2,&x,2);}
  Frame bench;bench.setIdentityProvider(&identity);
  for(unsigned i=0;bench.snapshotCacheEntries()<2231&&i<20000;++i){bench.clearFrame();Draw v{D3DPT_TRIANGLELIST,0,0,bv,3*(i%16),900-16-(i/16)%880,true};std::shared_ptr<const Mesh> m;assert(bench.read(&b,&b.decl,v,mesh,&why,false,&m));}
  const size_t entries=bench.snapshotCacheEntries(),bytes=bench.snapshotCacheBytes(),plans=bench.indexCacheEntries();
  auto t0=std::chrono::steady_clock::now();bench.setSnapshotCacheLimit(Frame::SnapshotCacheLimit/2);auto t1=std::chrono::steady_clock::now();
  bench.clearIndexCache();auto t2=std::chrono::steady_clock::now();assert(bench.snapshotCacheEntries()==0);
  std::printf("BENCH snapshot entries=%zu bytes=%zu indexPlans=%zu halveMs=%.3f clearMs=%.3f\n",entries,bytes,plans,
   std::chrono::duration<double,std::milli>(t1-t0).count(),std::chrono::duration<double,std::milli>(t2-t1).count());}
 std::printf("PASS model snapshot/index cache trim: full=%zu B; half cap evicts now and holds; shared holder valid; clear rebuilds identical bytes (miss then hit); cap restored\n",full);
}
'''

terrain=literal('test_terrain_snapshot.py','harness').split('int main(){')[0]+r'''
#include <chrono>
int main(){
    Device d;const Position a{-10200,-1180,3},b{-10198,-1180,4},c{-10200,-1178,5};
    d.vb.bytes.resize(16+200005u*24);d.put(4,a);d.put(100004,b);d.put(200004,c);
    std::uint32_t wide[]={4,100004,200004};d.ib.desc.Format=D3DFMT_INDEX32;d.ib.bytes.resize(sizeof wide);std::memcpy(d.ib.bytes.data(),wide,sizeof wide);
    FrameCache cache;MeshSnapshot out;Diagnostics why;
    auto read=[&](UINT i){cache.clearFrame();return cache.readMesh(&d,D3DPT_TRIANGLELIST,0,4,200001+i,0,1,d.view,out,&why);};
    const std::uint64_t limit=32u*1024u*1024u;assert(cache.persistentByteLimit()==limit);
    for(UINT i=0;i<12;++i)assert(read(i));assert(cache.persistentBytes()<=limit&&cache.persistentEntries()>=4);
    const auto fullEntries=cache.persistentEntries();const auto fullBytes=cache.persistentBytes();
    assert(read(11)&&why.contentCacheHit);const auto reference=out.positions;const auto indices=out.indices;
    cache.setPersistentByteLimit(limit/2);assert(cache.persistentByteLimit()==limit/2&&cache.persistentBytes()<=limit/2&&cache.persistentEntries()<fullEntries);
    assert(read(11)&&why.contentCacheHit); // newest entry kept
    for(UINT i=0;i<12;++i){assert(read(i));assert(cache.persistentBytes()<=limit/2);}
    cache.setPersistentByteLimit(0);assert(cache.persistentByteLimit()>0); // clamped: never an unusable cap
    cache.setPersistentByteLimit(limit*4);assert(cache.persistentByteLimit()==limit);
    cache.clearPersistent();assert(cache.persistentEntries()==0);
    assert(read(11)&&!why.contentCacheHit&&out.positions.size()==reference.size());
    for(size_t i=0;i<reference.size();++i)assert(out.positions[i].x==reference[i].x&&out.positions[i].y==reference[i].y&&out.positions[i].z==reference[i].z);
    assert(out.indices==indices);assert(read(11)&&why.contentCacheHit);
    d.put(4,{a.x,a.y,77});assert(read(11)&&!why.contentCacheHit&&out.positions[0].z==77); // rebuilt entries still verify live bytes
    assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks&&d.vb.refs==1&&d.ib.refs==1);
    {// Trim cost at production-like size (log: 2410 entries, 32 MiB).
     Device b;const UINT span=560;b.vb.bytes.resize(16+(span+4000)*24);for(UINT v=0;v<span+4000;++v)b.put(v,a);b.put(283,Position{-10198,-1180,4});b.put(563,Position{-10200,-1178,5});
     std::uint32_t tri[]={4,283,563};b.ib.desc.Format=D3DFMT_INDEX32;b.ib.bytes.resize(sizeof tri);std::memcpy(b.ib.bytes.data(),tri,sizeof tri);
     FrameCache bench;for(UINT i=0;i<2410;++i){bench.clearFrame();if(!bench.readMesh(&b,D3DPT_TRIANGLELIST,0,4,span+i,0,1,b.view,out,&why)){std::printf("reason=%d i=%u\\n",int(why.reason),i);assert(false);}}
     const auto entries=bench.persistentEntries();const auto bytes=bench.persistentBytes();
     auto t0=std::chrono::steady_clock::now();bench.setPersistentByteLimit(limit/2);auto t1=std::chrono::steady_clock::now();
     bench.clearPersistent();auto t2=std::chrono::steady_clock::now();assert(bench.persistentEntries()==0);
     std::printf("BENCH terrain entries=%zu bytes=%llu halveMs=%.3f clearMs=%.3f\n",entries,(unsigned long long)bytes,
      std::chrono::duration<double,std::milli>(t1-t0).count(),std::chrono::duration<double,std::milli>(t2-t1).count());}
    std::printf("PASS terrain content cache trim: full entries=%zu bytes=%llu; half cap evicts oldest now and holds; clamp; clear rebuilds identical snapshot, hits again, mutations still detected\n",fullEntries,(unsigned long long)fullBytes);
}
'''

gpu_stub=stub.replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
gpu=r'''
#include "replay_gpu_cache.h"
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
static size_t alive=0;
template<class T,class D>struct Buffer final:T{
 unsigned refs=1;std::vector<unsigned char> data;D desc;
 Buffer(size_t n):data(n){desc.Size=UINT(n);alive+=n;}~Buffer(){alive-=data.size();}
 unsigned AddRef()override{return ++refs;}unsigned Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT GetDesc(D* d)override{*d=desc;return D3D_OK;}
 HRESULT Lock(UINT o,UINT n,void** p,DWORD)override{if(o+n>data.size())return E_POINTER;*p=data.data()+o;return D3D_OK;}
 HRESULT Unlock()override{return D3D_OK;}
};
struct Device:IDirect3DDevice9{
 HRESULT CreateVertexBuffer(UINT n,DWORD,UINT,unsigned,IDirect3DVertexBuffer9** out,void*)override{*out=new Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>(n);return D3D_OK;}
 HRESULT CreateIndexBuffer(UINT n,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9** out,void*)override{*out=new Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>(n);return D3D_OK;}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return E_POINTER;}HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)override{return E_POINTER;}HRESULT GetStreamSourceFreq(UINT,UINT*)override{return E_POINTER;}HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)override{return E_POINTER;}HRESULT GetIndices(IDirect3DIndexBuffer9**)override{return E_POINTER;}
};
static std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh(unsigned bytes,unsigned seed){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->streams[0].stride=24;m->streams[0].bytes.resize(bytes);m->indices={0,1,2};for(unsigned i=0;i<bytes;++i)m->streams[0].bytes[i]=std::uint8_t(i*7+seed);return m;}
int main(){
 NorthlightReplayGPU::createBudgetMs()=1e9; // exact-count test: no time bound
 NorthlightReplayGPU::Cache cache;Device d;IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;
 auto release=[&](){for(auto& p:vb){if(p)p->Release();p=nullptr;}if(ib)ib->Release();ib=nullptr;};auto admit=[](size_t){return true;};
 std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> many;for(unsigned i=0;i<80;++i)many.push_back(mesh(1024*1024,i));
 for(unsigned frame=0;frame<40;++frame){cache.beginFrame();for(auto& x:many){cache.bind(&d,x,vb,ib,admit);release();}}
 const size_t full=cache.bytes();assert(full>32u*1024u*1024u&&full<=64u*1024u*1024u&&cache.limit()==64u*1024u*1024u);
 // A replay of the finished frame still holds its own reference while the cap drops.
 cache.beginFrame();bool bound=false;for(auto& x:many)if(cache.bind(&d,x,vb,ib,admit)){bound=true;break;}assert(bound);
 IDirect3DVertexBuffer9* held=vb[0];held->AddRef();release();
 cache.setLimit(32u*1024u*1024u);assert(cache.bytes()<=32u*1024u*1024u&&cache.limit()==32u*1024u*1024u);
 void* p=nullptr;assert(held->Lock(0,24,&p,0)==D3D_OK);held->Unlock(); // still alive through the holder's reference
 for(unsigned frame=0;frame<40;++frame){cache.beginFrame();for(auto& x:many){cache.bind(&d,x,vb,ib,admit);release();}assert(cache.bytes()<=32u*1024u*1024u);}
 assert(alive>=cache.bytes()&&alive<=cache.bytes()+1024u*1024u); // residency plus at most the one held buffer
 held->Release();assert(alive==cache.bytes()); // every evicted buffer was freed once its holder let go
 // Trim: clear frees every cache-owned buffer; the same mesh re-proves reuse, then uploads identical bytes.
 cache.clear();assert(cache.bytes()==0&&alive==0);
 cache.beginFrame();assert(!cache.bind(&d,many[5],vb,ib,admit));cache.beginFrame();assert(cache.bind(&d,many[5],vb,ib,admit));
 assert(vb[0]->Lock(0,1024*1024,&p,0)==D3D_OK&&!std::memcmp(p,many[5]->streams[0].bytes.data(),1024*1024));vb[0]->Unlock();release();
 cache.setLimit(size_t(1)<<40);assert(cache.limit()==64u*1024u*1024u);
 many.clear();cache.beginFrame();assert(cache.bytes()==0&&alive==0);
 {// Trim cost with ~1.8k resident meshes (log: 1806 resident, 43 MB). Fake buffers: DXVK Release cost is not modelled.
  NorthlightReplayGPU::Cache bench;std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> set;for(unsigned i=0;i<1806;++i)set.push_back(mesh(23000+(i%64)*16,i));
  for(unsigned frame=0;frame<200;++frame){bench.beginFrame();for(auto& x:set){bench.bind(&d,x,vb,ib,admit);release();}}
  const auto population=bench.population();const size_t bytes=bench.bytes();
  auto t0=std::chrono::steady_clock::now();bench.setLimit(32u*1024u*1024u);auto t1=std::chrono::steady_clock::now();
  bench.clear();auto t2=std::chrono::steady_clock::now();assert(bench.bytes()==0);
  std::printf("BENCH gpu resident=%zu entries=%zu bytes=%zu buffersReleased~%zu halveMs=%.3f clearMs=%.3f\n",population.resident,population.entries,bytes,population.resident*2,
   std::chrono::duration<double,std::milli>(t1-t0).count(),std::chrono::duration<double,std::milli>(t2-t1).count());}
 std::printf("PASS MODEL GPU cache trim: full=%zu B; half cap evicts now (buffers freed, holders keep theirs) and holds; clear frees all; re-bind uploads identical bytes; cap clamped\n",full);
}
'''

handoff=r'''
#include "async_memory_diagnostics.h"
#include "memory_guard.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
using namespace NorthlightMemoryGuard;
// Sampler thread produces exact walks (a scripted falling/recovering address space);
// the "render" thread drives the production request/take/update handoff and trims.
static std::atomic<unsigned> walks{0};static std::atomic<std::uint32_t> clock_{0};
static NorthlightMemoryDiagnostics::Sample walk(void*){
    const unsigned n=walks.fetch_add(1);NorthlightMemoryDiagnostics::Sample s;
    const std::uint64_t largest=(n/40)%2?120*MiB:900*MiB; // alternating pressure phases
    s.availableVirtual=1500*MiB;s.largestFree=largest;s.regions=3000;s.tick=clock_.load();s.valid=true;
    std::this_thread::sleep_for(std::chrono::microseconds(100));return s;
}
int main(){
    Guard guard;unsigned trims=0,entered=0,recovered=0,taken=0;bool pressure=false;
    {NorthlightMemoryDiagnostics::Sampler sampler(&walk);
     for(unsigned frame=0;frame<20000;++frame){
        const std::uint32_t now=clock_.fetch_add(50)+50; // 50 ms "frames"
        if(guard.requestDue(now))sampler.request();
        NorthlightMemoryDiagnostics::Sample s;
        if(sampler.take(s)){++taken;assert(s.valid);auto d=guard.update(s.availableVirtual,s.largestFree,std::uint32_t(s.tick));
            if(d.entered){++entered;assert(!pressure);pressure=true;}
            if(d.recovered){++recovered;assert(pressure);pressure=false;}
            if(d.trim){++trims;guard.trimCompleted(now);}
        }
        std::this_thread::sleep_for(std::chrono::microseconds(20)); // let the walk finish within a few frames
     }} // destruction joins the sampler with a request possibly pending
    std::printf("walks=%u taken=%u entered=%u recovered=%u trims=%u\n",walks.load(),taken,entered,recovered,trims);
    assert(taken>100&&entered>=2&&recovered>=1&&trims>=1&&pressure==guard.pressure());
    std::printf("PASS sampler/guard handoff under TSan: walks=%u taken=%u entered=%u recovered=%u trims=%u\n",walks.load(),taken,entered,recovered,trims);
}
'''
r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text();inl=fp.src('world_memory_guard.inl').read_text()
finish=r[r.index('    void finishFrameImpl() {'):r.index('    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND wnd')]
trim=r[r.index('    void trimMemory(const NorthlightMemoryDiagnostics::Sample& trigger){'):r.index('    void finishFrame() {')]
checks={
 'sampler always created (no diagnostics gate), below-normal walk':'try{memoryDiagnostics=std::make_unique<NorthlightMemoryDiagnostics::Sampler>(&queryAddressSpace);}' in r and 'SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL)' in r,
 'render thread requests on the guard cadence, never walks':'if(memoryGuard.requestDue(now))memoryDiagnostics->request();' in finish and 'VirtualQuery' not in finish,
 'caps and trim only after clearFrame() (frame boundary)':re.search(r'clearFrame\(\);\}\n        if\(memoryCaps>=0&&world\)world->setMemoryPressure\(memoryCaps==1\);\n        if\(memoryCaps>=0\)NorthlightStream::memoryPressure\.store\(memoryCaps==1,std::memory_order_relaxed\);[^\n]*\n        if\(memoryTrim\)trimMemory\(memorySample\);',finish) is not None and finish.count('trimMemory(memorySample)')==1 and finish.count('setMemoryPressure(')==1,
 'caps follow pressure transitions':'memoryCaps=decision.entered?1:0;' in finish,
 'periodic line gated, events ungated':'if(decision.report&&diagnostics())logf("MEMORY frame=' in finish and 'logf("MEMORY guard %s' in finish and 'if(decision.afterTrim)logf(' in finish,
 'trim: world caches, exact-walk before values, estimates, no address-space ballast':'t=world->trimMemory();' in trim and 'memoryGuard.trimCompleted(now);' in trim and 'logf("MEMORY guard trim frame=%u exactWalk=1 beforeAvailableVirtualMiB=' in trim and all(x not in r for x in ['Ballast','ballast','MEM_RESERVE','VirtualAlloc(']),
 'guard fed only by the exact full walk':'memoryGuard.update(sample.availableVirtual,sample.largestFree,DWORD(sample.tick))' in finish and 'auto& sample=memorySample;' in finish and 'if(memoryTrim)trimMemory(memorySample);' in finish and 'geometryAdmission' not in finish,
 'world trim: terrain, snapshot+index, pool, GPU cache, worker flag':all(x in inl for x in ['terrainBoundsCache.clearPersistent();','replaySnapshots.clearIndexCache();','freeReplays.clear();','pooledSnapshotBytes=0;','replayGpuCache.clear();','workerMemoryTrim.store(true']),
 'world caps: half under pressure, full on recovery':all(x in inl for x in ['setSnapshotCacheLimit(on?NorthlightDrawSnapshot::Frame::SnapshotCacheLimit/2:NorthlightDrawSnapshot::Frame::SnapshotCacheLimit)','setPersistentByteLimit(on?16u*1024u*1024u:32u*1024u*1024u)','replayGpuCache.setLimit(on?32u*1024u*1024u:64u*1024u*1024u)','memoryPressure?ReplayPoolBytes/2:ReplayPoolBytes']),
 'replay pool cap: no underflow above a lowered cap':'bool keep=capacity<=replayPoolLimit()-std::min(replayPoolLimit(),pooledSnapshotBytes);' in w,
 'worker drops only its own local-geometry cache, at request start':w.count('if(workerMemoryTrim.exchange(false,std::memory_order_relaxed))localGeometry.reset();')==1 and w.index('workerMemoryTrim.exchange')<w.index('auto result=std::make_shared<Snapshot>();result->map=r.map;'),
 'guard included inside WorldRenderer':'#include "world_memory_guard.inl"' in w,
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-memory-guard-') as tmp:
    t=Path(tmp)
    for name,body,header in [('guard',guard,stub),('handoff',handoff,stub),('snapshot',snapshot,stub),('terrain',terrain,stub),('gpu',gpu,gpu_stub)]:
        d=t/name;d.mkdir();(d/'d3d9.h').write_text(header);(d/'test.cpp').write_text(body)
        modes=[('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]
        if name=='handoff':modes.append(('tsan',['-O1','-g','-fsanitize=thread','-pthread']))
        for label,flags in modes:
            exe=d/label
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,'-I',str(d),*fp.test_include_flags(),str(d/'test.cpp'),'-o',str(exe)],check=True)
            out=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300);print(f'{name} {label}: '+out.stdout+out.stderr,end='');out.check_returncode()
