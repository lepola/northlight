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
stub=stub.replace('struct IDirect3DDevice9{','constexpr HRESULT S_OK=0,S_FALSE=1;constexpr unsigned D3DLOCK_DISCARD=8192,D3DISSUE_END=1;enum D3DQUERYTYPE{D3DQUERYTYPE_EVENT=8};\nstruct IDirect3DQuery9:IRef{virtual HRESULT Issue(DWORD)=0;virtual HRESULT GetData(void*,DWORD,DWORD)=0;};\nstruct IDirect3DDevice9{\nvirtual HRESULT CreateQuery(D3DQUERYTYPE,IDirect3DQuery9**)=0;\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
body=r'''
#include "replay_gpu_cache.h"
#include "replay_gpu_batches.h"
#include "replay_bulk_layout.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <climits>
static size_t alive=0;static unsigned freshLocks=0,plainLocks=0,overlaps=0; /* resident uploads; asserted by main only: fixture users run archived flags-0 caches */
#include <map>
struct DynLock{const void* buffer;UINT first,bytes;DWORD flags;};static std::vector<DynLock> dynLog; /* 0.3.192: every Lock of a dynamic (ring) buffer */
static unsigned issues=0,creates=0;static bool raceCheck=false,autoFrame=true;static unsigned frameNow=0,queryLatency=0;static const unsigned Never=1u<<30; /* fake GPU: a span of call N is reusable from frame N+1+queryLatency */
static std::map<const void*,const unsigned*> registry;static unsigned long long addRefs=0; /* 0.3.176 (U3'): every live buffer's count */
template<class T,class D>struct Buffer:T{
 unsigned refs=1;std::vector<unsigned char> data;D desc;bool fail=false;bool dynamic=false;std::vector<std::pair<UINT,UINT>> written;struct Busy{UINT first,bytes,frame;};std::vector<Busy> busy;unsigned dynLocks=0;
 Buffer(size_t n):data(n){desc.Size=UINT(n);alive+=n;registry[static_cast<T*>(this)]=&refs;}~Buffer(){alive-=data.size();registry.erase(static_cast<T*>(this));}
 unsigned AddRef()override{++addRefs;return ++refs;}unsigned Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT GetDesc(D* d)override{*d=desc;return D3D_OK;}
 HRESULT Lock(UINT o,UINT n,void** p,DWORD flags)override{if(fail||o+n>data.size())return E_POINTER;
  if(dynamic){ // 0.3.192 ring buffers: DISCARD or NOOVERWRITE only, a fresh buffer starts with NOOVERWRITE, and the fake GPU model below
   assert(flags==D3DLOCK_DISCARD||flags==D3DLOCK_NOOVERWRITE);assert(dynLocks||flags==D3DLOCK_NOOVERWRITE);
   if(raceCheck){bool pending=false;
    for(auto it=busy.begin();it!=busy.end();){if(it->frame+1+queryLatency<=frameNow)it=busy.erase(it);else{pending=pending||(o<it->first+it->bytes&&it->first<o+n);++it;}}
    if(flags==D3DLOCK_NOOVERWRITE)assert(!pending); /* the race check: never write over a region whose draws may still be reading it */
    else{assert(pending);busy.clear();} /* a DISCARD renames the buffer (the old slice stays with in-flight draws) and is only legal while something overlaps */
    busy.push_back({o,n,frameNow});}
   ++dynLocks;dynLog.push_back({this,o,n,flags});*p=data.data()+o;return D3D_OK;}
  // Resident buffers are fresh and written once: NOOVERWRITE (upload_lock.h), disjoint ranges. Test readbacks are READONLY.
  if(!dynamic&&flags!=D3DLOCK_READONLY){(flags==D3DLOCK_NOOVERWRITE?freshLocks:plainLocks)+=1;for(auto r:written)overlaps+=!(o+n<=r.first||o>=r.second);written.push_back({o,o+n});}
  *p=data.data()+o;return D3D_OK;}
 HRESULT Unlock()override{return D3D_OK;}
};
struct FakeQuery:IDirect3DQuery9{unsigned refs=1,issued=~0u;unsigned AddRef()override{return ++refs;}unsigned Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT Issue(DWORD)override{assert(issued==~0u);issued=frameNow;++issues;return D3D_OK;}HRESULT GetData(void* out,DWORD bytes,DWORD flags)override{assert(!out&&!bytes&&!flags);return issued!=~0u&&frameNow>=issued+1+queryLatency?S_OK:S_FALSE;}}; /* 0.3.192: the frame fence is Issued at the END of frame N and completes at frame N+1+queryLatency; never before it was issued */
struct Device:IDirect3DDevice9{
 unsigned calls=0,failAt=0;bool lockFail=false;
 HRESULT CreateQuery(D3DQUERYTYPE,IDirect3DQuery9** out)override{*out=new FakeQuery;++creates;return D3D_OK;}
 HRESULT CreateVertexBuffer(UINT n,DWORD usage,UINT,unsigned,IDirect3DVertexBuffer9** out,void*)override{if(++calls==failAt)return E_POINTER;auto p=new Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>(n);p->fail=lockFail;p->dynamic=usage&D3DUSAGE_DYNAMIC;*out=p;return D3D_OK;}
 HRESULT CreateIndexBuffer(UINT n,DWORD usage,D3DFORMAT,unsigned,IDirect3DIndexBuffer9** out,void*)override{if(++calls==failAt)return E_POINTER;auto p=new Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>(n);p->fail=lockFail;p->dynamic=usage&D3DUSAGE_DYNAMIC;*out=p;return D3D_OK;}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return E_POINTER;}HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**)override{return E_POINTER;}HRESULT GetStreamSourceFreq(UINT,UINT*)override{return E_POINTER;}HRESULT GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)override{return E_POINTER;}HRESULT GetIndices(IDirect3DIndexBuffer9**)override{return E_POINTER;}
};
std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh(unsigned bytes=240){auto m=std::make_shared<NorthlightDrawSnapshot::Mesh>();m->streams[0].stride=24;m->streams[0].bytes.resize(bytes);if(!SINGLE_STREAM){m->streams[1].stride=8;m->streams[1].bytes.resize(80);}m->indices={0,1,2};for(unsigned i=0;i<bytes;++i)m->streams[0].bytes[i]=i%251;return m;}
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

source=fp.src('world_renderer.h').read_text()
start=source.index('    bool uploadReplay(bool allowCache=true){');end=source.index('    bool actorMaterial',start)
method=source[start:end]
shell=r"""
#include <cstdarg>
#include <chrono>
struct LARGE_INTEGER {std::int64_t QuadPart=0;};
bool QueryPerformanceCounter(LARGE_INTEGER* out){out->QuadPart=std::chrono::steady_clock::now().time_since_epoch().count();return true;}
#include "capture_phase_profile.h"
#include "streaming_phase_profile.h"
#include "dynamic_ring.h"
#include "lock_meter.h"
namespace NorthlightGeometryMemory {constexpr size_t MiB=1024*1024;}
void logf(const char*,...){}
template<class T>void drop(T*& p){if(p)p->Release();p=nullptr;}
class WorldRenderer {public:
 struct Replay {
  std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared;
  NorthlightDrawSnapshot::Mesh snapshot;
  bool gpuCached=false,indexed=true;
  IDirect3DVertexBuffer9* stream[4]={};IDirect3DIndexBuffer9* index=nullptr;
  UINT offset[4]={},stride[4]={},start=0;INT base=0;
  const NorthlightDrawSnapshot::Mesh& mesh()const{return shared?*shared:snapshot;}
  ~Replay(){for(auto& p:stream)drop(p);drop(index);}
 };
 IDirect3DDevice9* d;REPLAY_CACHE replayGpuCache;
 NorthlightReplayBulk::Layout replayBulkLayout;
 std::vector<std::unique_ptr<Replay>> replays;
 IDirect3DVertexBuffer9* replayVerticesGPU[4]={};IDirect3DIndexBuffer9* replayIndicesGPU=nullptr;
 UINT replayVertexBytes[4]={},replayIndexBytes=0;NorthlightDynamicRing::Ring replayVertexRing[4],replayIndexRing;bool captureSampled=false;
 NorthlightStreaming::PhaseProfile streamingPhases;unsigned replayCreatedPeak=0;uint64_t replayTimeDeferred=0;uint64_t replayGrowths=0,replayFallbacks=0,replayBudgetOverrides=0;std::vector<size_t> replayTimeDeferredIndices; /* 0.3.138 split timers */
 NorthlightCapturePhases::Stats<2> replayUploadPhases;
 unsigned replayUploadCalls=0,replayUploadFallbacks=0;
 NorthlightDynamicRing::FrameFence frameFence; /* the production member of WorldRenderer */
 NorthlightDynamicRing::FrameFence& fence(){frameFence.device=d;return frameFence;}
 /* One frame: the production call, then the frame fence's Issue (WorldRenderer::endFrame). autoFrame: frames advance by themselves (tests that do not set frameNow). */
 bool upload(bool allowCache=true){const bool ok=uploadReplay(allowCache);frameFence.endFrame();if(autoFrame)++frameNow;return ok;}
 bool forceBulkPressure=false,denyAllGrowth=false;
 bool admitsGrowth(const char* kind,size_t){if(denyAllGrowth&&!std::strcmp(kind,"replay-growth"))return false;return !forceBulkPressure||std::strcmp(kind,"replay-growth")||replayGpuCache.bytes()==0;}
 bool check(HRESULT h,const char*){return !FAILED(h);}
 void deferLogf(const char*,...){} /* 0.3.176: the sampled MODEL GPU lines are written from endFrame */
 bool prepareUnsettled()const{return false;}void recycleReplay(Replay* raw){delete raw;} /* 0.3.177: no abandoned prepare worker */
 static UINT roundBuffer(UINT bytes,size_t){return bytes;} // exact allocations in fake device
 explicit WorldRenderer(IDirect3DDevice9* dev):d(dev){}
 ~WorldRenderer(){frameFence.drop();replays.clear();replayGpuCache.clear();for(auto& p:replayVerticesGPU)drop(p);drop(replayIndicesGPU);}
 void add(std::shared_ptr<const NorthlightDrawSnapshot::Mesh> m,bool cached){
  auto p=std::make_unique<Replay>();if(cached)p->shared=m;else p->snapshot=*m;replays.push_back(std::move(p));}
 // 0.3.176 (U3'): at a frame boundary every live buffer holds its owner's reference (the cache entry,
 // its batch or the bulk buffer member) plus one per replay binding: the 0.3.175 balance.
 void balance(){std::map<const void*,unsigned> held;
  for(auto& p:replays){for(auto* s:p->stream)if(s)++held[s];if(p->index)++held[p->index];}
  for(auto& b:registry){auto h=held.find(b.first);assert(*b.second==1+(h==held.end()?0:h->second));}
  for(auto& h:held)assert(registry.count(h.first));}
 void verify(){for(auto& p:replays){const auto& m=p->mesh();for(unsigned s=0;s<4;++s){if(m.streams[s].bytes.empty()){assert(!p->stream[s]);continue;}
  auto* actual=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(p->stream[s]);
  /* BaseVertexIndex (indexed) or StartVertex (non-indexed) of batched residency */
  assert(!memcmp(actual->data.data()+p->offset[s]+size_t(p->indexed?p->base:INT(p->start))*p->stride[s],m.streams[s].bytes.data(),m.streams[s].bytes.size()));assert(p->stride[s]==m.streams[s].stride);}
  if(p->indexed){auto* indices=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(p->index);
  assert(!memcmp(indices->data.data()+4*p->start,m.indices.data(),m.indices.size()*4));}else assert(!p->index&&(p->gpuCached||p->start==0));}}
"""
main=r"""
};
int main(){Device d;auto a=mesh(),b=mesh(480),c=mesh(96);
 // First-use/probation snapshots can repeat for many material/pose draws.
 // Every draw survives and sees its original bytes, with one shared upload.
 {WorldRenderer world(&d);for(unsigned i=0;i<4096;++i)world.add(a,true);
  assert(world.upload());world.verify();assert(world.replays.size()==4096);
  assert(world.replayVertexBytes[0]==(a->streams[0].bytes.empty()?0u:NorthlightDynamicRing::ringCapacity(a->streams[0].bytes.size())));
  assert(world.replayVertexBytes[1]==(a->streams[1].bytes.empty()?0u:NorthlightDynamicRing::ringCapacity(a->streams[1].bytes.size())));
  assert(world.replayIndexBytes==NorthlightDynamicRing::ringCapacity(a->indices.size()*4));
  for(auto& p:world.replays)assert(p->offset[0]==0&&p->start==0&&!p->gpuCached);
 }
 assert(alive==0);
 // Non-indexed draws share vertex ranges without inventing index buffers.
 {WorldRenderer world(&d);auto nonindexed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*a);nonindexed->indices.clear();nonindexed->indexed=false;
  world.add(nonindexed,true);world.add(nonindexed,true);for(auto& p:world.replays)p->indexed=false;
  assert(world.upload());world.verify();assert(world.replayIndexBytes==0);
 }
 assert(alive==0);
 // Under hard memory pressure the prefix fit counts UNIQUE bytes; repeated
 // draws referencing its first mesh all survive with their original order.
 {WorldRenderer world(&d);world.add(a,false);assert(world.upload());world.replays.clear();
  auto fresh=mesh();world.add(fresh,true);world.add(fresh,true);world.add(fresh,true);world.add(mesh(3<<20),false); /* 0.3.192: larger than the ring capacity of the first call */
  world.denyAllGrowth=true;assert(world.upload());world.verify();assert(world.replays.size()==3);
 }
 assert(alive==0);
 // Equal bytes with different immutable owners must NOT alias; UP/owned
 // snapshots remain distinct. Reordering and a new frame cannot reuse offsets.
 {WorldRenderer world(&d);auto equal=mesh();world.add(a,true);world.add(equal,true);world.add(a,true);world.add(a,false);
  assert(world.upload());world.verify();assert(world.replays[0]->offset[0]==world.replays[2]->offset[0]);
  assert(world.replays[1]->offset[0]!=world.replays[0]->offset[0]&&world.replays[3]->offset[0]!=world.replays[0]->offset[0]);
  world.replays.clear();auto fresh=mesh(672);world.add(fresh,true);world.add(fresh,true);world.add(a,true);
  assert(world.upload());world.verify();assert(world.replays[0]->offset[0]==world.replayVertexRing[0].spans.back().begin&&world.replays[1]->offset[0]==world.replays[0]->offset[0]); /* 0.3.192: absolute = the ring base of this call + the layout offset 0 */
 }
 assert(alive==0);
 for(bool sampled:{false,true}){WorldRenderer world(&d);world.captureSampled=sampled;
  world.add(a,true);world.add(b,false);world.add(c,true);assert(world.upload());world.verify();assert(!world.replays[0]->gpuCached);
  world.replays.clear();world.add(c,true);world.add(b,false);world.add(a,true);assert(world.upload());world.verify();assert(world.replays[0]->gpuCached&&!world.replays[1]->gpuCached&&world.replays[2]->gpuCached);
  auto* persistent=world.replays[2]->stream[0];world.replays.clear();world.add(a,true);world.add(c,true);assert(world.upload());world.verify();assert(world.replays[0]->stream[0]==persistent&&world.replayGpuCache.uploaded()==0);
  // Mutated/new immutable snapshot falls back without disturbing warm peers.
  auto changed=mesh(336);world.replays.clear();world.add(changed,true);world.add(a,true);assert(world.upload());world.verify();assert(!world.replays[0]->gpuCached&&world.replays[1]->gpuCached);
  // Optional residency is evicted before the old bulk path can drop a caster.
  world.forceBulkPressure=true;world.replays.clear();world.add(a,true);world.add(mesh(3<<20),false);assert(world.upload());world.verify();assert(world.replays.size()==2&&world.replayGpuCache.bytes()==0);world.forceBulkPressure=false;
  world.replays.clear();world.add(changed,true);assert(world.upload());world.verify();
  // Optional cache partial failure: old bulk path still uploads all objects.
  world.replays.clear();world.add(changed,true);world.add(b,false);d.failAt=d.calls+2;assert(world.upload());world.verify();assert(!world.replays[0]->gpuCached);
  if(sampled){assert(world.replayUploadCalls>0&&world.replayUploadFallbacks==1&&world.replayUploadPhases.clockReads>0);}
  else{assert(world.replayUploadCalls==0&&world.replayUploadFallbacks==0&&world.replayUploadPhases.clockReads==0);}
  d.failAt=0;
 }
 assert(alive==0);
 // 0.3.138 creation time bound: with a zero budget only the first resident
 // creation per frame proceeds; every other draw is served by the bulk path
 // with byte-identical content (verify), and residency still converges.
 {const double saved=NorthlightReplayGPU::createBudgetMs();NorthlightReplayGPU::createBudgetMs()=0;
  WorldRenderer world(&d);std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> ms;for(unsigned i=0;i<6;++i)ms.push_back(mesh(96+48*i));
  for(unsigned frame=0;frame<8;++frame){world.replays.clear();for(auto& m:ms)world.add(m,true);assert(world.upload());world.verify();
   unsigned cached=0;for(auto& p:world.replays)cached+=p->gpuCached;
   if(frame==0)assert(cached==0);else assert(cached==std::min(frame,6u)&&world.replayGpuCache.stats().created==(frame<=6?1u:0u));
   if(frame>=1&&frame<6)assert(world.replayGpuCache.stats().timeDeferred==6-frame);}
  NorthlightReplayGPU::createBudgetMs()=saved;}
 // Deferral must never force bulk growth: when the deferred meshes would not
 // fit the existing bulk capacity they are created exactly as 0.3.137 would.
 {const double saved=NorthlightReplayGPU::createBudgetMs();NorthlightReplayGPU::createBudgetMs()=0;
  WorldRenderer world(&d);std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> ms;for(unsigned i=0;i<6;++i)ms.push_back(mesh(96+48*i));
  for(auto& m:ms)world.add(m,true);assert(world.upload());world.verify(); /* warmup: all bulk */
  const UINT capacity=world.replayVertexBytes[0],indexCapacity=world.replayIndexBytes;
  world.replays.clear();for(auto& m:ms)world.add(m,true);auto fresh=mesh((2u<<20)-512);world.add(fresh,true); /* new mesh: bulk this frame; fits the 0.3.192 ring capacity alone, not together with the six deferred ones */
  assert(world.upload());world.verify();
  assert(world.replayBudgetOverrides==5&&world.replayVertexBytes[0]==capacity&&world.replayIndexBytes==indexCapacity);
  for(size_t i=0;i<6;++i)assert(world.replays[i]->gpuCached);assert(!world.replays[6]->gpuCached);
  /* Fits: deferral stays in effect, no forced creation. */
  world.replays.clear();auto other=mesh(96);world.add(other,true);world.add(fresh,true);assert(world.upload());world.verify();assert(world.replayBudgetOverrides==5);
  NorthlightReplayGPU::createBudgetMs()=saved;}
 // Crowd arrival: waves of new meshes exhaust the bulk capacity while many
 // warm meshes are time-deferred. Growth count and resident set must equal
 // the unbudgeted (0.3.137) run frame by frame; every frame is byte-verified.
 {const double saved=NorthlightReplayGPU::createBudgetMs();
  auto crowd=[&](double budget,std::vector<uint64_t>& growths,std::vector<unsigned>& cachedPerFrame,uint64_t& overrides){
   NorthlightReplayGPU::createBudgetMs()=budget;WorldRenderer world(&d);std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> pool;
   for(unsigned frame=0;frame<12;++frame){for(unsigned i=0;i<9;++i)pool.push_back(mesh((96+48*((frame*9+i)%7))*1024)); /* 9 new per frame; KiB-scale so the 0.3.192 ring capacities (>= 2 MiB) still grow */
    world.replays.clear();for(auto& m:pool)world.add(m,true);assert(world.upload());world.verify();
    unsigned cached=0;for(auto& p:world.replays)cached+=p->gpuCached;growths.push_back(world.replayGrowths);cachedPerFrame.push_back(cached);}
   overrides=world.replayBudgetOverrides;};
  std::vector<uint64_t> g0,g1;std::vector<unsigned> c0,c1;uint64_t o0=0,o1=0;
  crowd(1e30,g0,c0,o0);crowd(0,g1,c1,o1);
  assert(g0==g1&&g0.back()>0&&o0==0&&o1>0&&c1.back()<=c0.back());std::printf("crowd: growths=%llu (both runs) overrides=%llu\n",(unsigned long long)g0.back(),(unsigned long long)o1); /* deferral caused no extra growth; the guard fired */
  NorthlightReplayGPU::createBudgetMs()=saved;}
 // 0.3.176 (U3'): replays kept across uploads (bind-loop rebinding, repeated frames) with random
 // additions, removals, cache/bulk switches, bulk growth and cache clears. Reference counts balance at
 // every frame boundary, and a repeated upload of unchanged bindings takes no reference at all.
 {std::mt19937 rng(1760);unsigned steady=0;
  for(unsigned run=0;run<6;++run){WorldRenderer world(&d);std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> pool;
   for(unsigned i=0;i<12;++i)pool.push_back(mesh(96+48*(rng()%9)));
   for(unsigned frame=0;frame<60;++frame){
    const unsigned change=rng()%6;
    if(change==0&&!world.replays.empty())world.replays.erase(world.replays.begin()+rng()%world.replays.size());
    else if(change==1)world.add(pool[rng()%pool.size()],rng()%4!=0);
    else if(change==2){pool[rng()%pool.size()]=mesh(96+48*(rng()%9));world.add(pool[rng()%pool.size()],true);}
    else if(change==3&&world.replays.size()<40)world.add(mesh(2400+48*(rng()%30)),rng()%2); /* bulk growth */
    const bool allowCache=rng()%11!=0;world.forceBulkPressure=rng()%13==0;
    assert(world.upload(allowCache));world.verify();world.balance();
    world.forceBulkPressure=false;
    for(unsigned repeat=0;repeat<3;++repeat){const auto before=addRefs;assert(world.upload(allowCache));world.verify();world.balance();
     if(repeat==2&&allowCache){assert(addRefs==before);++steady;}}
   }}
  assert(steady>200&&alive==0&&registry.empty());std::printf("refcount balance: %u steady re-uploads took no reference\n",steady);}

 // 0.3.192 (DXVK3): multi-frame behaviour of the bulk rings against a fake GPU. A span of call N is reusable again
 // queryLatency+1 frames later (FakeQuery); the dynamic fakes check every flag: DISCARD or NOOVERWRITE only, NOOVERWRITE
 // on a fresh buffer, never a NOOVERWRITE over a region whose draws are still in flight, DISCARD only while one is.
 raceCheck=true;autoFrame=false;
 struct Roles{unsigned discards[5]={},locks[5]={};UINT first[5]={};bool seen[5]={};};
 auto roles=[&](WorldRenderer& world,size_t from){Roles r;std::map<const void*,unsigned> perBuffer;
  for(size_t i=from;i<dynLog.size();++i){const auto& l=dynLog[i];++perBuffer[l.buffer];assert(perBuffer[l.buffer]==1); /* one Lock, so at most one DISCARD, per buffer and call */
   int role=-1;for(unsigned s=0;s<4;++s)if(l.buffer==world.replayVerticesGPU[s])role=int(s);if(l.buffer==world.replayIndicesGPU)role=4;
   assert(role>=0);++r.locks[role];r.discards[role]+=l.flags==D3DLOCK_DISCARD;r.first[role]=l.first;r.seen[role]=true;assert(l.first%(role==4?4u:16u)==0);}
  return r;};
 // The same non-cached set for 50 frames, the Replay objects kept: verify() every frame, offsets advance and wrap, no DISCARD
 // while the 3 MiB ring (five ~600 KiB sets) has its old span done by the time the cursor wraps back.
 for(unsigned late:{0u,1u}){
  WorldRenderer world(&d);dynLog.clear();queryLatency=late;frameNow=0;world.add(mesh(600*1024),false);world.add(mesh(),false);
  unsigned advances=0,wraps=0;UINT previous=0;
  for(unsigned f=0;f<50;++f){frameNow=f;const size_t from=dynLog.size();assert(world.upload());world.verify();world.balance();
   const auto r=roles(world,from);assert(r.seen[0]&&r.seen[4]&&!r.discards[0]&&!r.discards[1]&&!r.discards[4]);
   const UINT base=world.replays[0]->offset[0];assert(base==r.first[0]&&world.replays[1]->offset[0]==base+614400&&base+614640<=world.replayVertexBytes[0]);
   advances+=base>previous;wraps+=f&&!base;previous=base;}
  assert(advances>=30&&wraps>=8&&world.replayGrowths==1+(SINGLE_STREAM?0u:1u)+1); /* streams 0 (and 1) and the index buffer, created once */
 }
 // Queries still pending in the first frames, then the GPU catches up: exactly one DISCARD per stream overall (the first wrap),
 // none afterwards, every later wrap reuses the start after the fence.
 {WorldRenderer world(&d);dynLog.clear();queryLatency=Never;frameNow=0;world.add(mesh(600*1024),false);world.add(mesh(),false);
  unsigned discards[5]={},lastDiscard=0;
  for(unsigned f=0;f<50;++f){frameNow=f;if(f==8)queryLatency=0;const size_t from=dynLog.size();assert(world.upload());world.verify();
   const auto r=roles(world,from);for(unsigned k=0;k<5;++k){discards[k]+=r.discards[k];if(r.discards[k])lastDiscard=f;}}
  assert(discards[0]==1&&lastDiscard==5&&!discards[1]&&!discards[2]&&!discards[3]&&!discards[4]);
  queryLatency=0;}
 // Shared duplicates with a non-zero ring base: every duplicate carries the owner's absolute offset and index start.
 {WorldRenderer world(&d);dynLog.clear();queryLatency=0;frameNow=0;auto shared=mesh(64*1024),equal=mesh(64*1024);
  world.add(mesh(100*1024),false);assert(world.upload());world.verify();assert(world.replayVertexRing[0].cursor==100*1024&&world.replayIndexRing.cursor==12);
  frameNow=1;world.replays.clear();world.add(shared,true);world.add(shared,true);world.add(equal,true);world.add(shared,true);world.add(a,false);
  assert(world.upload());world.verify();world.balance();
  const UINT base=world.replayVertexRing[0].spans.back().begin,ibBase=world.replayIndexRing.spans.back().begin;assert(base==100*1024&&ibBase==12);
  assert(!world.replays[0]->gpuCached&&!world.replays[2]->gpuCached);
  assert(world.replays[0]->offset[0]==base&&world.replays[1]->offset[0]==base&&world.replays[3]->offset[0]==base&&world.replays[2]->offset[0]==base+64*1024&&world.replays[4]->offset[0]==base+128*1024);
  assert(world.replays[0]->start==3&&world.replays[1]->start==3&&world.replays[3]->start==3&&world.replays[2]->start==6&&world.replays[4]->start==9);
  for(const auto& l:dynLog)assert(l.flags==D3DLOCK_NOOVERWRITE);}
 // Pressure-fit prefix (growth refused) with a non-zero base, then a wrap that reuses the start after the fence.
 {WorldRenderer world(&d);dynLog.clear();queryLatency=0;frameNow=0;world.add(mesh(1536*1024),false);assert(world.upload());world.verify();
  const UINT capacity=world.replayVertexBytes[0];assert(capacity==NorthlightDynamicRing::ringCapacity(1536*1024)&&capacity==(6u<<20));
  frameNow=1;world.replays.clear();auto fresh=mesh(4096);world.add(fresh,true);world.add(fresh,true);world.add(fresh,true);world.add(mesh(6<<20),false);world.denyAllGrowth=true;
  assert(world.upload());world.verify();assert(world.replays.size()==3&&world.replayVertexBytes[0]==capacity);
  assert(world.replays[0]->offset[0]==1536*1024&&world.replays[1]->offset[0]==1536*1024&&world.replays[2]->offset[0]==1536*1024&&world.replays[0]->start>0);
  frameNow=2;world.replays.clear();world.add(mesh(4608*1024),false);assert(world.upload());world.verify();assert(world.replays[0]->offset[0]==0&&world.replayVertexBytes[0]==capacity);
  for(const auto& l:dynLog)assert(l.flags==D3DLOCK_NOOVERWRITE);}
 // The frame fence: one CreateQuery for all the rings placed in a frame (up to four vertex streams and the index buffer), its Issue only at the
 // frame end, no query and no Issue in a frame without a placement, and the spans of the frame are not done before that Issue.
 {WorldRenderer world(&d);queryLatency=0;frameNow=0;world.add(mesh(),false);const unsigned c0=creates,i0=issues;
  assert(world.uploadReplay());assert(creates==c0+1&&issues==i0);
  for(auto& r:world.replayVertexRing)for(auto& sp:r.spans)assert(sp.query&&!world.fence().done(sp.query));
  assert(!world.replayIndexRing.spans.empty()&&!world.fence().done(world.replayIndexRing.spans.back().query));
  world.frameFence.endFrame();assert(issues==i0+1);
  frameNow=1;world.replays.clear();assert(world.uploadReplay());world.frameFence.endFrame();assert(creates==c0+1&&issues==i0+1);
  assert(world.fence().done(world.replayIndexRing.spans.back().query));}
 // gpuCached replays keep their batch/entry offsets, starts and bases untouched while a bulk replay moves with the ring.
 {WorldRenderer world(&d);dynLog.clear();queryLatency=0;frameNow=0;world.add(mesh(),true);world.add(mesh(96),true);world.add(mesh(300*1024),false);
  struct Snap{IDirect3DVertexBuffer9* stream[4];IDirect3DIndexBuffer9* index;UINT offset[4],start;INT base;};std::vector<Snap> kept;UINT bulkOffset=0;unsigned bulkMoves=0;
  for(unsigned f=0;f<14;++f){frameNow=f;assert(world.upload());world.verify();world.balance();
   for(size_t i=0;i<2&&f>=4;++i){auto& p=*world.replays[i];assert(p.gpuCached);
    Snap now{};for(unsigned k=0;k<4;++k){now.stream[k]=p.stream[k];now.offset[k]=p.offset[k];}now.index=p.index;now.start=p.start;now.base=p.base;
    if(f==4)kept.push_back(now);else{const auto& was=kept[i];for(unsigned k=0;k<4;++k)assert(now.stream[k]==was.stream[k]&&now.offset[k]==was.offset[k]);assert(now.index==was.index&&now.start==was.start&&now.base==was.base);}}
   assert(!world.replays[2]->gpuCached);bulkMoves+=world.replays[2]->offset[0]!=bulkOffset;bulkOffset=world.replays[2]->offset[0];}
  assert(kept.size()==2&&bulkMoves>=2);}
 raceCheck=false;autoFrame=true;queryLatency=0;frameNow=0;
 assert(alive==0&&freshLocks>0&&!plainLocks&&!overlaps);puts("PASS production uploadReplay: mixed cache/bulk offsets, reordered draws, all-cached zero upload, modified geometry, allocation-failure fallback and COM cleanup; creation time bound keeps bulk bytes identical and never forces bulk growth");
}
"""
integration=body.split('int main()')[0]+shell+method+main

with tempfile.TemporaryDirectory(prefix='northlight-replay-upload-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(integration)
 # Per-mesh cache (switch off), batched cache with multi-stream meshes (per-mesh layout) and single-stream meshes (batches).
 for variant in [['-DREPLAY_CACHE=NorthlightReplayGPU::Cache','-DSINGLE_STREAM=0'],['-DREPLAY_CACHE=NorthlightReplayGPU::BatchedCache','-DSINGLE_STREAM=0'],['-DREPLAY_CACHE=NorthlightReplayGPU::BatchedCache','-DSINGLE_STREAM=1']]:
  for flags in [[],['-fsanitize=address,undefined','-fno-omit-frame-pointer']]:
   subprocess.run(['clang++','-std=c++17','-O2',*variant,*flags,'-I'+str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   print(' '.join(variant[:2]),' '.join(flags[:1]),flush=True);subprocess.run([str(p/'test')],check=True)
