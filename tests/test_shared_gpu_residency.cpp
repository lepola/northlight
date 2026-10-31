// Real cache implementations, independent bound-buffer byte oracle. No game/GPU.
#include "replay_fixture.h"
#include "replay_gpu_batches.h"
#include <chrono>
#include <fstream>
#include <string>
using Mesh=NorthlightDrawSnapshot::Mesh;
using Owner=std::shared_ptr<const Mesh>;
struct FrozenMeshLayout {NorthlightSourceIdentity::Handle sourceIdentity,replayIdentity;NorthlightDrawSnapshot::Stream streams[4];std::vector<uint32_t> indices;UINT vertexCount,primitiveCount;D3DPRIMITIVETYPE topology;bool indexed,dynamic;};
static_assert(sizeof(FrozenMeshLayout)==sizeof(Mesh),"Shared hint must use existing padding");
struct Binding {
 IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;
 ~Binding(){for(auto p:vb)if(p)p->Release();if(ib)ib->Release();}
};
static auto admit=[](size_t){return true;};
static bool exact(const Mesh& mesh,const Binding& b){
 for(unsigned s=0;s<4;++s){const auto& bytes=mesh.streams[s].bytes;if(bytes.empty()){if(b.vb[s])return false;continue;}
  if(!b.vb[s])return false;const auto& data=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(b.vb[s])->data;
  const size_t offset=s?0:size_t(b.at.vertexBase)*mesh.streams[s].stride;
  if(offset+bytes.size()>data.size()||std::memcmp(data.data()+offset,bytes.data(),bytes.size()))return false;
 }
 if(mesh.indices.empty())return !b.ib;
 if(!b.ib)return false;const auto& data=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(b.ib)->data;
 const size_t offset=size_t(b.at.indexStart)*4;
 return offset+mesh.indices.size()*4<=data.size()&&!std::memcmp(data.data()+offset,mesh.indices.data(),mesh.indices.size()*4);
}
static Owner authored(unsigned id,bool multi=false,bool indexed=true){
 auto m=std::make_shared<Mesh>();m->topology=D3DPT_TRIANGLELIST;m->indexed=indexed;m->sharedCapture=true;m->vertexCount=192;m->primitiveCount=64;
 m->streams[0].stride=48;m->streams[0].bytes.resize(192*48);
 for(size_t i=0;i<m->streams[0].bytes.size();++i)m->streams[0].bytes[i]=uint8_t((id*7+i*11)%251);
 if(multi){m->streams[1].stride=8;m->streams[1].bytes.resize(192*8,uint8_t(id));}
 if(indexed)for(unsigned i=0;i<192;++i)m->indices.push_back((i+id*3)%192);
 m->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,id,multi,indexed});return m;
}
static std::vector<Owner> load(const char* path){
 std::ifstream file(path,std::ios::binary);assert(file);uint32_t count=0;file.read(reinterpret_cast<char*>(&count),4);assert(count&&count<1024);
 std::vector<Owner> result;for(uint32_t i=0;i<count;++i){
  uint32_t sizes[2];file.read(reinterpret_cast<char*>(sizes),8);assert(sizes[0]&&sizes[0]%48==0&&sizes[1]%3==0);
  auto m=std::make_shared<Mesh>();m->streams[0].stride=48;m->streams[0].bytes.resize(sizes[0]);m->indices.resize(sizes[1]);
  file.read(reinterpret_cast<char*>(m->streams[0].bytes.data()),sizes[0]);file.read(reinterpret_cast<char*>(m->indices.data()),sizes[1]*4);assert(file);
  m->indexed=true;m->vertexCount=sizes[0]/48;m->primitiveCount=sizes[1]/3;
  for(auto n:m->indices)assert(n<m->vertexCount);
  m->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,1000+i});result.push_back(m);
 }return result;
}
static void controls(){
 using namespace NorthlightReplayGPU;Device d;Cache material;BatchedCache world;
 auto a=authored(1),b=authored(2),multi=authored(3,true),plain=authored(4,false,false);std::vector<Owner> list{a,b,multi,plain};
 Binding held;Placement at;const auto worldSource=residentSource(world),materialSource=residentSource(material);
 for(auto& m:list){Binding x;assert(!worldSource.borrow(m,x.vb,x.ib,x.at));assert(!materialSource.borrow(m,x.vb,x.ib,x.at));}
 assert(!d.calls&&!world.population().entries&&!material.population().entries); // Borrow cannot promote or allocate.
 world.beginFrame();material.beginFrame();for(auto& m:list){Binding x;assert(!world.bind(&d,m,x.vb,x.ib,admit,x.at));}
 world.beginFrame();for(auto& m:list){Binding x;world.bind(&d,m,x.vb,x.ib,admit,x.at);}world.commitPending();
 const auto creates=d.calls,locks=freshLocks;bool baseControl=false,indexControl=false;
 for(auto& m:list){Binding x;assert(worldSource.borrow(m,x.vb,x.ib,x.at)&&exact(*m,x));
  const auto saved=x.at;if(saved.vertexBase){x.at.vertexBase=0;baseControl|=!exact(*m,x);x.at=saved;}
  if(saved.indexStart){x.at.indexStart=0;indexControl|=!exact(*m,x);x.at=saved;}}
 assert(baseControl&&indexControl&&d.calls==creates&&freshLocks==locks);
 a->sharedCapture=false;{Binding x;assert(!worldSource.borrow(a,x.vb,x.ib,x.at));assert(world.borrow(a,x.vb,x.ib,x.at)&&exact(*a,x));}a->sharedCapture=true;
 // Different content/layout/revision has a new owner. Exact source reconstruction is accepted only by the identity-aware cache.
 auto changed=std::make_shared<Mesh>(*a);changed->streams[0].bytes[0]^=1;changed->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,1,1});
 {Binding x;assert(!worldSource.borrow(changed,x.vb,x.ib,x.at)&&!materialSource.borrow(changed,x.vb,x.ib,x.at));}
 material.beginFrame();assert(!material.bind(&d,a,held.vb,held.ib,admit));material.beginFrame();assert(material.bind(&d,a,held.vb,held.ib,admit));
 auto reconstructed=std::make_shared<Mesh>(*a);{Binding x;assert(materialSource.borrow(reconstructed,x.vb,x.ib,x.at)&&exact(*reconstructed,x));assert(!worldSource.borrow(reconstructed,x.vb,x.ib,x.at));}
 Binding heldBatch;assert(worldSource.borrow(b,heldBatch.vb,heldBatch.ib,heldBatch.at));world.clear();assert(exact(*b,heldBatch));
 assert(!worldSource.borrow(b,heldBatch.vb,heldBatch.ib,at)&&exact(*b,heldBatch));
 // Held references survive eviction/reset; a cache miss does not alter them.
 assert(materialSource.borrow(a,held.vb,held.ib,held.at));material.setLimit(0);world.clear();assert(exact(*a,held));
 assert(!materialSource.borrow(a,held.vb,held.ib,at)&&exact(*a,held));
 auto source=ResidentSource{};assert(!source.borrow(a,held.vb,held.ib,at));
 material.clear();assert(exact(*a,held));
 std::puts("SHARED_GPU_CONTROLS_PASS no_admission_or_device_work exact_offsets multi_stream nonindexed changed_revision reconstructed_owner reset_held_refs null_provider negative_base negative_start");
}
struct Result {double ms=0;size_t uploaded=0,up=0,peak=0,borrowed=0;unsigned creates=0;uint64_t digest=0;};
static Result run(const std::vector<Owner>& meshes,bool shared,bool audit,unsigned frames,unsigned overlap=100){
 using namespace NorthlightReplayGPU;Device device;Cache material;BatchedCache world;material.setLimit(32u<<20);
 std::vector<Owner> worldMeshes,materialMeshes;
 for(size_t i=0;i<meshes.size();++i){auto m=std::make_shared<Mesh>(*meshes[i]);m->sharedCapture=i*100/meshes.size()<overlap;worldMeshes.push_back(m);materialMeshes.push_back(m);
  if(!m->sharedCapture){auto other=std::make_shared<Mesh>(*m);other->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,2000+i});materialMeshes.back()=other;}}
 const auto materialSource=residentSource(material),worldSource=residentSource(world);Result r;
 const auto begin=std::chrono::steady_clock::now();
 for(unsigned frame=0;frame<frames;++frame){
  material.beginFrame();world.beginFrame();
  // Material flush precedes world shadow uploads, matching the renderer's order.
  for(size_t j=0;j<materialMeshes.size();++j){const auto& m=materialMeshes[(j+(frame/24)*13)%materialMeshes.size()];Binding b;
   const bool borrowed=shared&&worldSource.borrow(m,b.vb,b.ib,b.at);r.borrowed+=borrowed;
   const bool ready=borrowed||material.bind(&device,m,b.vb,b.ib,admit);
   if(ready){if(audit)assert(exact(*m,b));}else r.up+=m->byteSize();
   r.digest=r.digest*33+uint64_t(m->vertexCount)*7+m->primitiveCount;
  }
  for(auto& m:worldMeshes){Binding b;const bool borrowed=shared&&materialSource.borrow(m,b.vb,b.ib,b.at);r.borrowed+=borrowed;
   const bool ready=borrowed||world.bind(&device,m,b.vb,b.ib,admit,b.at);if(ready&&audit)assert(exact(*m,b));
  }
  world.commitPending();
  for(auto& m:worldMeshes){Binding b;bool ready=shared&&materialSource.borrow(m,b.vb,b.ib,b.at);if(!ready)ready=world.bind(&device,m,b.vb,b.ib,admit,b.at);
   if(ready){if(audit)assert(exact(*m,b));}else r.up+=m->byteSize();
  }
  r.uploaded+=material.uploaded()+world.uploaded();r.peak=std::max(r.peak,material.bytes()+world.bytes());
  assert(material.chargedBytes()<=material.limit()&&world.bytes()<=world.limit());
  if(frame%47==46){material.trimTo(0);world.trimTo(0);} // Pressure preserves the exact fallback.
 }
 r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();r.creates=device.calls;
 material.clear();world.clear();assert(!alive&&!overlaps&&!plainLocks);return r;
}
int main(int argc,char** argv){
 assert(argc>=2);NorthlightReplayGPU::createBudgetMs()=1e6;auto meshes=load(argv[1]);
 if(argc>=3){const bool shared=std::string(argv[2])=="shared";auto r=run(meshes,shared,false,96,argc>3?unsigned(std::stoul(argv[3])):100);
  std::printf("SHARED_GPU_TIMING shared=%u cpuMs=%.6f uploads=%zu fallbackBytes=%zu peak=%zu borrowed=%zu creates=%u digest=%llu\n",shared,r.ms,r.uploaded,r.up,r.peak,r.borrowed,r.creates,(unsigned long long)r.digest);return 0;}
 controls();assert(!alive);
 for(unsigned overlap:{0u,50u,100u}){auto old=run(meshes,false,true,96,overlap),next=run(meshes,true,true,96,overlap);
  assert(old.digest==next.digest&&next.uploaded<=old.uploaded&&next.peak<=old.peak&&next.creates<=old.creates&&next.up<=old.up);
  if(overlap)assert(next.borrowed&&next.uploaded<old.uploaded&&next.peak<old.peak&&next.creates<old.creates);
  else assert(!next.borrowed&&next.uploaded==old.uploaded&&next.peak==old.peak&&next.creates==old.creates&&next.up==old.up);
  std::printf("SHARED_GPU_EXACT_PASS overlap=%u meshes=%zu uploads=%zu->%zu fallback=%zu->%zu peak=%zu->%zu creates=%u->%u shared=%zu digest=%llu\n",overlap,meshes.size(),old.uploaded,next.uploaded,old.up,next.up,old.peak,next.peak,old.creates,next.creates,next.borrowed,(unsigned long long)next.digest);
 }
 return 0;
}
