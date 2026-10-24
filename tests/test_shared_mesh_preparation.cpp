#include "world_mesh_pages.h"
#include "shadow_terrain.h"
#include "regional_shadow_range.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <chrono>
namespace NorthlightWorldMesh {bool buildFrozenUploadPlan(const NorthlightGI::WorldScene&,WorldMeshUploadPlan&,std::string&,const NorthlightGI::AllocationAdmission&,const std::vector<NorthlightGI::WorldMaterial>*,const PlanOrder&);}
#include "frozen_shadow_terrain.h"
using namespace NorthlightGI;namespace M=NorthlightWorldMesh;namespace P=NorthlightWorldMeshPages;
static unsigned checks=0;
static void sameBatch(const M::Batch&a,const M::Batch&b){assert(a.start==b.start&&a.count==b.count&&a.material==b.material&&a.chunkX==b.chunkX&&a.chunkY==b.chunkY&&a.terrain==b.terrain);assert(!std::memcmp(&a.boundsLow,&b.boundsLow,sizeof(Vec3))&&!std::memcmp(&a.boundsHigh,&b.boundsHigh,sizeof(Vec3)));++checks;}
static void same(const M::WorldMeshUploadPlan&a,const M::WorldMeshUploadPlan&b){
 assert(a.vertexCount==b.vertexCount&&a.triangleCount==b.triangleCount&&a.indices==b.indices&&a.batches.size()==b.batches.size()&&a.fixedTerrainChunks==b.fixedTerrainChunks&&a.unmaskableTerrainTriangles==b.unmaskableTerrainTriangles&&a.materials.size()==b.materials.size());
 assert(a.source->vertices.size()==b.source->vertices.size()&&!std::memcmp(a.source->vertices.data(),b.source->vertices.data(),a.source->vertices.size()*sizeof(WorldVertex)));
 for(size_t i=0;i<a.batches.size();++i)sameBatch(a.batches[i],b.batches[i]);
 for(size_t i=0;i<a.materials.size();++i){const auto&x=a.materials[i];const auto&y=b.materials[i];assert(x.width==y.width&&x.height==y.height&&x.alphaCutoff==y.alphaCutoff&&x.terrain==y.terrain&&x.bgra==y.bgra&&x.textureIdentity==y.textureIdentity);++checks;}
 assert(bool(a.localShadowRecords)==bool(b.localShadowRecords));if(a.localShadowRecords){assert(a.localShadowRecords->size()==b.localShadowRecords->size());for(size_t i=0;i<a.localShadowRecords->size();++i){const auto&x=(*a.localShadowRecords)[i];const auto&y=(*b.localShadowRecords)[i];assert(x.content==y.content&&x.chunkX==y.chunkX&&x.chunkY==y.chunkY&&x.terrain==y.terrain&&!std::memcmp(&x.low,&y.low,sizeof(Vec3))&&!std::memcmp(&x.high,&y.high,sizeof(Vec3)));++checks;}}
}
static void samePages(const P::Plan&a,const P::Plan&b){assert(a.pages.size()==b.pages.size()&&a.batches.size()==b.batches.size()&&a.vertexBytes==b.vertexBytes&&a.indexBytes==b.indexBytes);for(size_t i=0;i<a.pages.size();++i){const auto&x=a.pages[i];const auto&y=b.pages[i];assert(x.key==y.key);if(!y.elided)assert(x.indices==y.indices&&x.vertices.size()==y.vertices.size()&&!std::memcmp(x.vertices.data(),y.vertices.data(),x.vertices.size()*sizeof(WorldVertex)));++checks;}for(size_t i=0;i<a.batches.size();++i){sameBatch(a.batches[i],b.batches[i]);assert(a.batches[i].page==b.batches[i].page&&a.batches[i].minVertex==b.batches[i].minVertex&&a.batches[i].vertexCount==b.batches[i].vertexCount);}}
static double elapsed(std::chrono::steady_clock::time_point start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
static std::vector<std::string> tiles(const std::string& root,const std::string& map,Vec3 center,int radius){std::vector<std::string> out;int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));for(int y=ty-radius;y<=ty+radius;++y)for(int x=tx-radius;x<=tx+radius;++x){auto p=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(std::filesystem::exists(p))out.push_back(p);}return out;}
int main(int argc,char**argv){assert(argc>=2);const std::string root=argv[1];bool bench=argc>2;std::map<std::string,uint64_t> ids;uint64_t reads=0;
 SceneSourceProvider provider=[&](const std::string&path,std::string&error,const AllocationAdmission&admit){auto scene=std::make_shared<WorldScene>();if(!loadScene(path.c_str(),*scene,error,admit))return std::shared_ptr<const WorldScene>{};auto id=ids.emplace(path,ids.size()+1).first->second;scene->sourceIdentity=id;++reads;return std::shared_ptr<const WorldScene>(scene);};
 NorthlightMeshPageKey::KeySet previousResident;M::PreparationCache cache;cache.setLimit(8u<<20);std::string error;const float reach=2048;const Vec3 origin(5194.73f,-48.32f,492.18f);
 for(unsigned iteration=0;iteration<(bench?7u:3u);++iteration){Vec3 center=origin+Vec3(iteration==2?32.f:iteration>2?64.f:0,0,0);WorldScene local,terrain;
  assert(loadInstancedScenes(tiles(root,"Northrend",center,1),root+"/models",center-Vec3(288,288,320),center+Vec3(288,288,320),local,error,15,{}, {},{},nullptr,provider));
  auto filter=[center](Vec3 low,Vec3 high){return NorthlightRegionalShadow::selected(center,low,high);};auto chunk=[center](Vec3 low,Vec3 high){return NorthlightRegionalShadow::selectedChunk(center,low,high);};
  assert(loadInstancedScenes(tiles(root,"Northrend",center,4),root+"/models",center-Vec3(reach,reach,reach),center+Vec3(reach,reach,reach),terrain,error,1,{},filter,chunk,nullptr,provider));
  M::WorldMeshUploadPlan baseline,current;double baseMs=0,newMs=0;
  const auto before=cache.stats();auto oldBuild=[&]{auto t=std::chrono::steady_clock::now();assert(FrozenShadowTerrain::build(local,terrain,center,baseline,error,{},reach));baseMs=elapsed(t);};auto newBuild=[&]{auto t=std::chrono::steady_clock::now();assert(NorthlightShadowTerrain::build(local,terrain,center,current,error,{},reach,{},false,nullptr,&cache));newMs=elapsed(t);};if(iteration&1){newBuild();oldBuild();}else{oldBuild();newBuild();}same(baseline,current);
  P::Plan oldPages,newPages,reused;auto t=std::chrono::steady_clock::now();assert(P::build(baseline,oldPages,error));double oldPageMs=elapsed(t);P::Options options;options.preparation=&cache;if(!previousResident.empty())options.resident=&previousResident;t=std::chrono::steady_clock::now();assert(P::build(current,newPages,error,{},options));double newPageMs=elapsed(t);samePages(oldPages,newPages);
  NorthlightMeshPageKey::KeySet resident;for(const auto&p:oldPages.pages)resident.insert(p.key);options.resident=&resident;t=std::chrono::steady_clock::now();assert(P::build(current,reused,error,{},options));double reuseMs=elapsed(t);samePages(oldPages,reused);assert(!P::validatePages(current,reused));
  const auto stats=cache.stats();std::printf("SHARED_MESH iteration=%u triangles=%u local=%zu far=%zu pieces=%zu prepared=%llu reused=%llu hits=%llu bytes=%llu oldPlanMs=%.6f newPlanMs=%.6f oldPagesMs=%.6f newPagesMs=%.6f residentPagesMs=%.6f pageHits=%llu pages=%zu checks=%u\n",iteration,current.triangleCount,local.triangles.size(),terrain.triangles.size(),local.sourcePieces.size()+terrain.sourcePieces.size(),(unsigned long long)(stats.preparedTriangles-before.preparedTriangles),(unsigned long long)(stats.reusedTriangles-before.reusedTriangles),(unsigned long long)(stats.hits-before.hits),(unsigned long long)stats.bytes,baseMs,newMs,oldPageMs,newPageMs,reuseMs,(unsigned long long)(stats.pageHits-before.pageHits),newPages.pages.size(),checks);std::fflush(stdout);
  assert(stats.bytes<=(8u<<20));if(iteration==1)assert(stats.reusedTriangles>before.reusedTriangles);
  // A missing active GPU page is never fabricated by a memo.
  previousResident=resident;resident.erase(oldPages.pages.front().key);P::Plan missing;assert(P::build(current,missing,error,{},options));samePages(oldPages,missing);assert(!missing.pages.front().elided);
  if(iteration==(bench?6u:2u)){const auto pinned=current.cpuBytes();cache.setLimit(0);assert(current.cpuBytes()==pinned);cache.setLimit(8u<<20);}
 }
 cache.reset();std::printf("PASS shared mesh exact checks=%u sourceReads=%llu\n",checks,(unsigned long long)reads);
}
