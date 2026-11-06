#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace M=NorthlightWorldMesh;using namespace NorthlightGI;
static WorldScene fixture(){WorldScene s;WorldMaterial m;m.width=2;m.height=1;m.rgba={1,2,3,0,4,5,6,255};s.materials.push_back(m);for(unsigned i=0;i<120;++i){float x=float(i%7)*64;s.vertices.push_back({{x,-0.f,0},{0,0,1},0,0});s.vertices.push_back({{x+1,0,0},{0,0,1},1,0});s.vertices.push_back({{x,1,0},{0,0,1},0,1});s.triangles.push_back({i*3,i*3+1,i*3+2,0});}return s;}
static void same(const M::WorldMeshUploadPlan&a,const M::WorldMeshUploadPlan&b){assert(a.indices==b.indices&&a.batches.size()==b.batches.size()&&a.materials.size()==b.materials.size());for(size_t i=0;i<a.batches.size();++i){auto x=a.batches[i],y=b.batches[i];assert(x.start==y.start&&x.count==y.count&&x.material==y.material&&x.chunkX==y.chunkX&&x.chunkY==y.chunkY&&x.terrain==y.terrain&&!std::memcmp(&x.boundsLow,&y.boundsLow,12)&&!std::memcmp(&x.boundsHigh,&y.boundsHigh,12));}for(size_t i=0;i<a.materials.size();++i)assert(a.materials[i].bgra==b.materials[i].bgra);}
int main(){auto scene=fixture();std::string error;M::WorldMeshUploadPlan base,first,second;assert(M::buildUploadPlan(scene,base,error));M::PreparationPiece piece;piece.identity="validated-source-1";piece.triangleCount=uint32_t(scene.triangles.size());piece.materials={0};std::vector<M::PreparationPiece> pieces{piece};M::PreparationCache cache;cache.setLimit(8u<<20);
 assert(M::buildUploadPlan(scene,first,error,{},nullptr,{},&pieces,&cache));same(base,first);const auto cold=cache.stats();assert(cold.misses==1&&cold.hits==0&&cold.bytes>0);assert(M::buildUploadPlan(scene,second,error,{},nullptr,{},&pieces,&cache));same(base,second);assert(cache.stats().hits==1);
 auto payload=first.preparationLayout;auto bytes=cache.stats().bytes;cache.reset();assert(cache.stats().hits==1&&cache.stats().bytes==bytes);
 // Compare the SAME plan with and without its layout: compact preparation can be smaller than the baseline's reserve.
 assert(payload&&payload->size()==1&&(*payload)[0].retainedBytes==bytes);
 const auto withLayout=first.cpuBytes();first.preparationLayout.reset();const auto withoutLayout=first.cpuBytes();first.preparationLayout=payload;
 const auto singleMetadata=sizeof(*payload)+payload->capacity()*sizeof(M::PreparationIdentity)+64+(*payload)[0].materials.capacity()*sizeof(uint32_t);
 assert(withLayout==withoutLayout+singleMetadata+bytes);
 // Two references to one immutable preparation owner charge its retained storage once.
 auto duplicate=std::make_shared<M::PreparationLayout>(*payload);duplicate->push_back(duplicate->front());first.preparationLayout=duplicate;
 const auto duplicateMetadata=sizeof(*duplicate)+duplicate->capacity()*sizeof(M::PreparationIdentity)+64+((*duplicate)[0].materials.capacity()+(*duplicate)[1].materials.capacity())*sizeof(uint32_t);
 assert(first.cpuBytes()==withoutLayout+duplicateMetadata+bytes);first.preparationLayout=payload;duplicate.reset();first={};second={};assert(cache.stats().bytes==bytes);payload.reset();assert(cache.stats().bytes==0);
 // Evicted caches cannot re-admit beyond their cap while a publication pins metadata.
 assert(M::buildUploadPlan(scene,first,error,{},nullptr,{},&pieces,&cache));bytes=cache.stats().bytes;cache.setLimit(bytes-1);assert(cache.stats().bytes==bytes&&cache.stats().entries==0);assert(M::buildUploadPlan(scene,second,error,{},nullptr,{},&pieces,&cache));same(base,second);assert(!second.preparationLayout);first={};second={};assert(cache.stats().bytes==0);cache.setLimit(8u<<20);
 // Weak local identity prevents reuse when an address's original owner expired.
 auto owner=std::make_shared<int>(1);pieces[0].owner=owner;pieces[0].weakIdentity=true;assert(M::buildUploadPlan(scene,first,error,{},nullptr,{},&pieces,&cache));owner.reset();const auto misses=cache.stats().misses;assert(M::buildUploadPlan(scene,second,error,{},nullptr,{},&pieces,&cache));assert(cache.stats().misses==misses+1);same(base,second);first={};second={};cache.reset();pieces[0]=piece;
 // Recency, not lexical identity, decides eviction under a two-piece cap.
 {M::PreparationCache lru; lru.setLimit(8u<<20);M::WorldMeshUploadPlan out;auto sources=pieces;
  auto build=[&](char name){sources[0].identity="validated-source-"+std::string(1,name);assert(M::buildUploadPlan(scene,out,error,{},nullptr,{},&sources,&lru));same(base,out);out={};};
  build('A');const auto one=lru.stats().bytes;lru.setLimit(one*2);build('B');assert(lru.stats().entries==2);build('A');auto hits=lru.stats().hits;build('C');assert(lru.stats().entries==2);build('A');assert(lru.stats().hits==hits+1);auto misses=lru.stats().misses;build('B');assert(lru.stats().misses==misses+1);
  // A boundary miss preceding a known far piece must not evict that later hit.
  build('A');auto combined=scene;combined.vertices.insert(combined.vertices.end(),scene.vertices.begin(),scene.vertices.end());for(auto t:scene.triangles){t.v0+=uint32_t(scene.vertices.size());t.v1+=uint32_t(scene.vertices.size());t.v2+=uint32_t(scene.vertices.size());combined.triangles.push_back(t);}
  auto current=sources;current[0].identity="validated-source-C";current.push_back(piece);current[1].identity="validated-source-B";current[1].firstTriangle=uint32_t(scene.triangles.size());hits=lru.stats().hits;misses=lru.stats().misses;assert(M::buildUploadPlan(combined,out,error,{},nullptr,{},&current,&lru));assert(lru.stats().hits==hits+1&&lru.stats().misses==misses+1);M::WorldMeshUploadPlan plain;assert(M::buildUploadPlan(combined,plain,error));same(plain,out);out={};
 }
 // A refusal in optional acceleration retries the baseline transactionally.
 for(unsigned refusal=1;refusal<24;++refusal){cache.reset();unsigned calls=0;M::WorldMeshUploadPlan out;assert(M::buildUploadPlan(scene,out,error,[&](uint64_t){return ++calls!=refusal;},nullptr,{},&pieces,&cache));same(base,out);}
 pieces[0].materials={999};assert(M::buildUploadPlan(scene,first,error,{},nullptr,{},&pieces,&cache));same(base,first);pieces[0]=piece;
 scene.triangles[0].v0=UINT32_MAX;M::WorldMeshUploadPlan sentinel=base;cache.reset();assert(!M::buildUploadPlan(scene,sentinel,error,{},nullptr,{},&pieces,&cache));same(base,sentinel);
 std::puts("PASS shared preparation lifetime: exact output, publication pins, eviction/reset RAII cap, actual LRU/preflight priority, weak expiry,23admission failures, malformed provenance and transactional invalid input");
}
