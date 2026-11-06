#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Actual intrusive GPU-cache list, byte/lifetime invariants and native benchmarks."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import hashlib,json,subprocess,tempfile
HERE=Path(__file__).resolve().parent
OUT=fp.output_dir()
import replay_gpu_fixture as gpu
stub=gpu.stub
harness=gpu.fixture+r'''
#include "baseline123.h"
#include "reference4096.h"
#include <chrono>
#include <random>
#include <string>
#include <unordered_set>
'''+gpu.helpers+r'''
// Inspect the actual stable nodes and map; no production testing hooks.
static void structure(const NorthlightReplayGPU::Cache& cache);
static const NorthlightReplayGPU::Cache::Entry* matchingEntry(const NorthlightReplayGPU::Cache& cache,const Mesh* mesh){
 const auto key=mesh->sourceIdentity?mesh->sourceIdentity->hash:std::uint64_t(reinterpret_cast<std::uintptr_t>(mesh));
 const auto range=cache.entries.equal_range(key);const NorthlightReplayGPU::Cache::Entry* found=nullptr;
 for(auto i=range.first;i!=range.second;++i){const auto& entry=*i->second;
  if(mesh->sourceIdentity?NorthlightSourceIdentity::same(entry.identity,mesh->sourceIdentity):!entry.identity&&entry.owner.lock().get()==mesh){assert(!found);found=&entry;}}
 return found;
}
static void limitsAndFailures(){
 assert(NorthlightReplayGPU::Cache::entryLimit()==4096);
 {NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;for(unsigned i=0;i<17;++i)ms.push_back(sized(256*1024,i));
  c.beginFrame();for(auto& m:ms)assert(!gpuBind(c,d,m,b,false));c.beginFrame();
  for(unsigned i=0;i<16;++i){assert(gpuBind(c,d,ms[i],b,false));b.clear();structure(c);}
  assert(!gpuBind(c,d,ms[16],b,false)&&c.uploaded()==4*MiB&&c.stats().attemptDeferred==1&&c.stats().uploadByteDeferred==0);structure(c);
  c.beginFrame();assert(gpuBind(c,d,ms[16],b,false));b.clear();
 }
 assert(!alive);
 {NorthlightReplayGPU::Cache c;Device d;Bindings b;auto a=sized(2*MiB),e=sized(2*MiB),f=sized(1);c.beginFrame();
  for(auto& m:{a,e,f})assert(!gpuBind(c,d,m,b,false));c.beginFrame();assert(gpuBind(c,d,a,b,false));b.clear();assert(gpuBind(c,d,e,b,false));b.clear();
  assert(!gpuBind(c,d,f,b,false)&&c.stats().uploadByteDeferred==1&&c.stats().attemptDeferred==0);structure(c);
 }
 assert(!alive);
 // All five allocation positions: four independent vertex streams plus indices.
 for(unsigned type=0;type<3;++type)for(unsigned failure=1;failure<=5;++failure){NorthlightReplayGPU::Cache c;Device d;Bindings b;
  auto mutableMesh=std::make_shared<Mesh>(*mesh());mutableMesh->streams[2].bytes={1,2,3,4};mutableMesh->streams[3].bytes={8,7,6,5};Owner m=mutableMesh;
  c.beginFrame();assert(!gpuBind(c,d,m,b));c.beginFrame();
  if(type==0)d.failAt=failure;else if(type==1)d.failLockAt=failure;else d.failUnlockAt=failure;
  assert(!gpuBind(c,d,m,b)&&!alive&&c.bytes()==0&&c.population().probation==1);structure(c);
  d.failAt=d.failLockAt=d.failUnlockAt=0;c.beginFrame();assert(gpuBind(c,d,m,b));structure(c);b.clear();c.clear();structure(c);assert(!alive);
 }
}
static void expiryAndReuse(){
 NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;for(unsigned i=0;i<7;++i)ms.push_back(sized(40+i,i));
 c.beginFrame();for(auto& m:ms)assert(!gpuBind(c,d,m,b));c.beginFrame();for(auto& m:ms){assert(gpuBind(c,d,m,b));b.clear();}structure(c);
 // Remove oldest, middle and newest nodes by weak-owner expiry.
 const size_t before=c.bytes(),removed=ms[0]->byteSize()+ms[3]->byteSize()+ms[6]->byteSize();
 ms[0].reset();ms[3].reset();ms[6].reset();c.beginFrame();structure(c);assert(c.stats().expiredEntries==3&&c.stats().expiredBytes==removed&&c.bytes()==before-removed);
 for(auto& m:ms)if(m){assert(gpuBind(c,d,m,b));b.clear();structure(c);}c.clear();structure(c);assert(!alive);
 alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto create=[&](unsigned salt){return Owner(new(storage) Mesh(*sized(64,salt)),[](const Mesh* p){const_cast<Mesh*>(p)->~Mesh();});};
 auto old=create(1);const Mesh* address=old.get();c.beginFrame();assert(!gpuBind(c,d,old,b));c.beginFrame();assert(gpuBind(c,d,old,b));std::weak_ptr<const Mesh> weak=old;old.reset();assert(weak.expired());
 auto replacement=create(2);assert(replacement.get()==address);assert(!gpuBind(c,d,replacement,b));structure(c);assert(alive==64); // held old output refs
 c.beginFrame();assert(gpuBind(c,d,replacement,b));verify(replacement,b,true);structure(c);
 c.clear();assert(alive==64);verify(replacement,b,true);b.clear();structure(c);replacement.reset();assert(!alive);
}
static void byteAndSlotPressure(){
 {NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;for(unsigned i=0;i<4097;++i)ms.push_back(sized(8,i));
  c.beginFrame();for(unsigned i=0;i<4096;++i)assert(!gpuBind(c,d,ms[i],b));structure(c);assert(!gpuBind(c,d,ms.back(),b));structure(c);
  c.beginFrame(); // Mark all existing entries current without allocating them.
  for(unsigned i=0;i<4096;++i)assert(!c.bind(&d,ms[i],b.vb,b.ib,[](size_t){return false;}));
  assert(!gpuBind(c,d,ms.back(),b));assert(gpuBind(c,d,ms[0],b));b.clear();structure(c);
  assert(c.population().entries==4096&&c.population().resident==1&&c.stats().evictions==0); // promotion needs no slot
  c.beginFrame();assert(!gpuBind(c,d,ms.back(),b));assert(c.stats().evictions==1);structure(c);
  c.clear();structure(c);assert(!alive);
 }
 {NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;
  const size_t metadata=NorthlightReplayGPU::Cache::QueueMetadata,payload=(64*MiB-18*metadata)/16;
  for(unsigned i=0;i<18;++i)ms.push_back(sized(payload,i));
  c.beginFrame();for(auto& m:ms)assert(!gpuBind(c,d,m,b,false));
  for(unsigned f=0;f<16;++f){c.beginFrame();for(unsigned i=0;i<16;++i){gpuBind(c,d,ms[i],b,false);b.clear();}structure(c);}
  assert(c.bytes()==16*payload&&c.chargedBytes()==64*MiB);c.beginFrame();for(unsigned i=0;i<16;++i){assert(gpuBind(c,d,ms[i],b,false));b.clear();}
  for(unsigned i=16;i<18;++i)assert(!c.bind(&d,ms[i],b.vb,b.ib,[](size_t){return false;}));
  assert(!gpuBind(c,d,ms[16],b,false)&&!gpuBind(c,d,ms[17],b,false));structure(c);assert(c.bytes()==16*payload&&c.chargedBytes()==64*MiB&&c.stats().evictions==0);
  // Even an unpromoted entry needs its charged links; current entries stay protected.
  auto tiny=sized(4);const auto rejected=c.stats().roomRejected;assert(!gpuBind(c,d,tiny,b));structure(c);
  assert(c.stats().roomRejected==rejected+1&&!matchingEntry(c,tiny.get())&&c.chargedBytes()==64*MiB);
  c.beginFrame();assert(gpuBind(c,d,ms[16],b,false));b.clear();assert(c.bytes()==16*payload&&c.chargedBytes()==64*MiB-metadata&&c.stats().evictions==1);structure(c);
 }
 assert(!alive);
}
static size_t stress(){
 NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;for(unsigned i=0;i<5600;++i)ms.push_back(sized(16+(i%5)*8,i));
 std::mt19937 random(7124);size_t checked=0;
 for(unsigned frame=0;frame<140;++frame){
  if(frame==40||frame==90)c.clear();
  for(unsigned j=0;j<31;++j){const size_t i=random()%ms.size();ms[i]=sized(16+(i%5)*8,frame+i);}
  c.beginFrame();structure(c);std::vector<size_t> order(ms.size());for(size_t i=0;i<order.size();++i)order[i]=i;std::shuffle(order.begin(),order.end(),random);
  std::unordered_set<const Mesh*> boundThisFrame;
  for(size_t i=0;i<order.size();++i){auto& m=ms[order[i]];const bool good=gpuBind(c,d,m,b);if(boundThisFrame.count(m.get()))assert(good);if(good)boundThisFrame.insert(m.get());b.clear();++checked;
   if(i%64==0){structure(c);for(auto& held:boundThisFrame)assert(matchingEntry(c,held));}
   if(i%127==0){const bool again=gpuBind(c,d,m,b);assert(!good||again);b.clear();++checked;structure(c);}
  }
  assert(c.chargedBytes()<=64*MiB&&c.uploaded()<=4*MiB&&c.population().entries<=4096);structure(c);
 }
 c.clear();structure(c);assert(!alive);return checked;
}
template<class C>static void benchmark(const char* version,unsigned count,bool churn){
 C c;Device d;Bindings b;std::vector<Owner> ms;const unsigned owners=churn?8192:count;for(unsigned i=0;i<owners;++i)ms.push_back(sized(churn?64:8192,i));
 uint64_t ns=0;size_t bulk=0,hits=0,uploaded=0,evicted=0,visits=0,passes=0;
 const unsigned frames=churn?100:300,sampleFrames=20;
 for(unsigned frame=0;frame<frames;++frame){c.beginFrame();const auto start=std::chrono::steady_clock::now();size_t frameBulk=0;
  for(unsigned j=0;j<count;++j){const auto& m=ms[churn?(frame*128+j)%owners:j];if(!gpuBind(c,d,m,b,false))frameBulk+=m->byteSize();b.clear();}
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  if(frame>=frames-sampleFrames){ns+=elapsed;bulk+=frameBulk;hits+=c.hits();uploaded+=c.uploaded();evicted+=c.stats().evictions;if constexpr(std::is_same<C,NorthlightReplayGPU::Cache>::value){visits+=c.stats().evictionChecks;passes+=c.stats().evictionChecks;}else{visits+=c.stats().scanVisits;passes+=c.stats().scanPasses;}}
 }
 if(!churn&&std::string(version)!="baseline123"&&count<=4096)assert(bulk==0&&c.population().resident==count);
 std::printf("{\"version\":\"%s\",\"workload\":\"%s\",\"packets\":%u,\"sampleFrames\":20,\"residentBytes\":%zu,\"bulkBytes\":%zu,\"newBytes\":%zu,\"hits\":%zu,\"evictions\":%zu,\"maintenancePasses\":%zu,\"maintenanceVisits\":%zu,\"bindNanoseconds\":%llu}\n",version,churn?"rotating-window":"stable",count,c.bytes(),bulk,uploaded,hits,evicted,passes,visits,(unsigned long long)ns);std::fflush(stdout);c.clear();assert(!alive);
}
static void structure(const NorthlightReplayGPU::Cache& c){
 assert((c.oldest_==nullptr)==c.entries.empty());assert((c.newest_==nullptr)==c.entries.empty());
 std::unordered_set<const void*> seen;size_t bytes=0,metadata=0,identity=0;const NorthlightReplayGPU::Cache::Entry* previous=nullptr;
 for(auto* e=c.oldest_;e;e=e->next){
  assert(seen.insert(e).second&&e->previous==previous&&e->touched<=c.frame_);
  if(previous)assert(previous->touched<=e->touched);
  const auto range=c.entries.equal_range(e->key);unsigned matches=0;for(auto i=range.first;i!=range.second;++i)matches+=i->second.get()==e;assert(matches==1);
  size_t payload=0;for(auto* v:e->vertices)if(v)payload+=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(v)->data.size();
  if(e->indices)payload+=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(e->indices)->data.size();assert(payload==e->bytes);
  assert(e->metadata>=c.QueueMetadata);metadata+=e->metadata;identity+=e->metadata-c.QueueMetadata;bytes+=e->bytes;previous=e;
 }
 assert(previous==c.newest_&&seen.size()==c.entries.size()&&bytes==c.gpuBytes_&&bytes==c.bytes());
 assert(bytes+metadata==c.bytes_&&c.chargedBytes()==bytes+metadata&&identity==c.stats().identityBytes);
 assert(c.queueBytes()==c.entries.size()*c.QueueMetadata&&metadata==identity+c.queueBytes()&&c.chargedBytes()<=c.limit());
 if(c.oldest_)assert(!c.oldest_->previous);if(c.newest_)assert(!c.newest_->next);
 size_t reverse=0;const NorthlightReplayGPU::Cache::Entry* next=nullptr;
 for(auto* e=c.newest_;e;e=e->previous){assert(e->next==next&&seen.count(e));next=e;++reverse;}
 assert(next==c.oldest_&&reverse==seen.size());
 std::unordered_set<const void*> probation;previous=nullptr;
 for(auto* e=c.probationOldest_;e;e=e->probationNext){assert(seen.count(e)&&probation.insert(e).second&&!e->bytes&&e->probationPrevious==previous);if(previous)assert(previous->touched<=e->touched);previous=e;}
 assert(previous==c.probationNewest_);next=nullptr;reverse=0;
 for(auto* e=c.probationNewest_;e;e=e->probationPrevious){assert(e->probationNext==next&&probation.count(e));next=e;++reverse;}
 assert(next==c.probationOldest_&&reverse==probation.size()&&probation.size()==c.population().probation);
 for(const auto& item:c.entries)assert(seen.count(item.second.get())&&item.first==item.second->key);
 for(const auto& item:c.entries)assert(bool(probation.count(item.second.get()))==!item.second->bytes);
 assert(!c.stats().scanVisits&&!c.stats().scanPasses&&!c.stats().scanMemoHits);
 assert(c.stats().evictionChecks==c.stats().evictions+c.stats().roomRejected);
}
static void stableNodesAndTouch(){
 NorthlightReplayGPU::Cache c;Device d;Bindings b;std::vector<Owner> ms;
 for(unsigned i=0;i<5;++i)ms.push_back(sized(32,i));
 c.beginFrame();for(auto& m:ms)assert(!gpuBind(c,d,m,b));
 std::vector<const void*> nodes;for(auto& m:ms){const auto* node=matchingEntry(c,m.get());assert(node);nodes.push_back(node);}
 c.beginFrame();for(size_t i=0;i<ms.size();++i){assert(gpuBind(c,d,ms[i],b));b.clear();assert(matchingEntry(c,ms[i].get())==nodes[i]);structure(c);}
 c.beginFrame();assert(gpuBind(c,d,ms[0],b));assert(c.stats().lruMoves==1);const auto* tail=c.newest_;
 assert(gpuBind(c,d,ms[0],b)&&c.stats().lruMoves==1&&tail==c.newest_);structure(c);
 b.clear();c.clear();structure(c);assert(!alive);
}
static void collidingIdentityNodes(){
 NorthlightReplayGPU::Cache c;Device d;Bindings b;
 auto first=std::const_pointer_cast<Mesh>(sized(64,17));first->sourceIdentity=NorthlightSourceIdentity::make({10,1});
 auto collision=std::make_shared<Mesh>(*first);collision->streams[0].bytes[0]^=1;
 const uint64_t inverse=14886173955864302971ull,seed=1469598103934665603ull,prime=1099511628211ull;
 const uint64_t last=(first->sourceIdentity->hash*inverse)^((seed^11)*prime);collision->sourceIdentity=NorthlightSourceIdentity::make({11,last});
 assert(first->sourceIdentity->hash==collision->sourceIdentity->hash&&!NorthlightSourceIdentity::same(first->sourceIdentity,collision->sourceIdentity));
 c.beginFrame();assert(!gpuBind(c,d,first,b)&&!gpuBind(c,d,collision,b));
 const auto* a=matchingEntry(c,first.get());const auto* z=matchingEntry(c,collision.get());assert(a&&z&&a!=z);structure(c);
 c.beginFrame();assert(gpuBind(c,d,first,b));b.clear();assert(gpuBind(c,d,collision,b));b.clear();
 assert(matchingEntry(c,first.get())==a&&matchingEntry(c,collision.get())==z);structure(c);
 auto reconstructed=std::make_shared<Mesh>(*first);std::weak_ptr<const Mesh> expired=first;first.reset();assert(expired.expired());
 c.beginFrame();const auto calls=d.calls;assert(gpuBind(c,d,reconstructed,b)&&d.calls==calls&&!c.uploaded());b.clear();
 assert(matchingEntry(c,reconstructed.get())==a&&matchingEntry(c,collision.get())==z&&c.entries.size()==2);structure(c);
 c.clear();structure(c);assert(!alive);
}
int main(){NorthlightReplayGPU::createBudgetMs()=1e30; /* exact 16-attempt/4 MiB limits; time bound tested in test_replay_create_budget.cpp */
 stableNodesAndTouch();collidingIdentityNodes();limitsAndFailures();expiryAndReuse();byteAndSlotPressure();const size_t checked=stress();std::fprintf(stderr,"PASS GPU maintenance: both lists/full-key map/age/payload/metadata invariants, colliding identities and reconstructed stable nodes, expiry and same-address replacement, held output references, full-slot promotion, current-frame protection, 64MiB/4MiB/16-attempt limits, 15 partial failure positions, %zu stress bind/content checks.\n",checked);
 for(unsigned n:{2048u,2800u,4096u,4500u}){benchmark<NorthlightReplayGPU123::Cache>("baseline123",n,false);benchmark<NorthlightReplayGPU4096Reference::Cache>("reference4096",n,false);benchmark<NorthlightReplayGPU::Cache>("production124",n,false);}
 benchmark<NorthlightReplayGPU123::Cache>("baseline123",256,true);benchmark<NorthlightReplayGPU4096Reference::Cache>("reference4096",256,true);benchmark<NorthlightReplayGPU::Cache>("production124",256,true);
}
'''
source=fp.src('replay_gpu_cache.h').read_text()
baselinePath=fp.FIXTURES/'replay-gpu-cache-0.3.123/replay_gpu_cache.baseline-0.3.123.h'
baseline=baselinePath.read_text()
assert 'EntryLimit=2048' in baseline
reference=baseline.replace('namespace NorthlightReplayGPU {','namespace NorthlightReplayGPU4096Reference {').replace('EntryLimit=2048','EntryLimit=4096')
baseline=baseline.replace('namespace NorthlightReplayGPU {','namespace NorthlightReplayGPU123 {')

files=[fp.tracked(name) for name in ['replay_gpu_cache.h','draw_snapshot.h','capture_buffer_metadata.h','test_replay_gpu_maintenance.py','replay_gpu_fixture.py','test_replay_gpu_cache.py','test_terrain_snapshot.py']]+[baselinePath]
report={'command':'python3 tests/test_replay_gpu_maintenance.py','scope':'Actual production cache and fake D3D buffers; exact archived .123 and temporary cap4096 reference. Private structural invariants use native clang -fno-access-control. No game/Wine or real GPU execution.','source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'notes':['Legacy maintenanceVisits counts inspected map entries; production maintenanceVisits/maintenancePasses both count constant-time head checks. Legacy pass count is a full scan count, so pass counts are not equal units.', 'Native timing includes bind and fake output Release only; excludes beginFrame expiry walk, bulk copy, real driver upload and rendering. Timing is informational, not an FPS claim.', 'Cap-only reference modifies exactly EntryLimit from 2048 to 4096. Equal-age victim ties intentionally differ between hash scan and first-touch queue; returned content and current-frame protection remain required.'], 'runs':[]}
with tempfile.TemporaryDirectory(prefix='gpu-maintenance-') as tmp:
 root=Path(tmp);(root/'d3d9.h').write_text(stub);(root/'test.cpp').write_text(harness);(root/'baseline123.h').write_text(baseline);(root/'reference4096.h').write_text(reference)
 for flags in [['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']]:
  subprocess.run(['clang++','-std=c++17','-fno-access-control','-Wall','-Wextra','-Werror',*flags,'-I'+str(root),*fp.test_include_flags(),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
  result=subprocess.run([str(root/'test')],text=True,capture_output=True);print(result.stdout,result.stderr,flush=True);result.check_returncode()
  report['runs'].append({'flags':flags,'exit_code':result.returncode,'stderr':result.stderr,'benchmarks':[json.loads(line) for line in result.stdout.splitlines()]})
(OUT/'gpu-maintenance-validation.json').write_text(json.dumps(report,indent=2)+'\n')
