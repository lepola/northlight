#include "shadow_terrain.h"
#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <new>
using namespace NorthlightGI;
static bool failNextAllocation=false;
void* operator new(size_t n){if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
int main(int argc,char**argv){
    assert(argc==2);const std::string root=argv[1];const Vec3 center(5872,672,650);
    const int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));
    std::vector<std::string> nearFiles,farFiles;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
        auto p=root+"/Northrend/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        if(std::filesystem::exists(p)){farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);}
    }
    LocalSceneCache localCache;BVH localBvh;WorldScene far;std::string error;
    assert(localCache.build(nearFiles,root+"/models","Northrend",center-Vec3(288,288,320),center+Vec3(288,288,320),localBvh,error,{},true));
    const auto& local=localBvh.scene();
    const auto cached=localCache.stats().retainedBytes;assert(cached>0);
    const auto radius=NorthlightShadowTerrain::Radius;
    assert(loadInstancedScenes(farFiles,root+"/models",center-Vec3(radius,radius,radius),center+Vec3(radius,radius,radius),far,error,1));
    WorldScene point;point.vertices={local.vertices[0],local.vertices[1],local.vertices[2]};point.triangles.push_back({0,1,2,0});point.materials.push_back(local.materials[0]);point.materials[0].lampSourceId=12345;
    NorthlightWorldMesh::WorldMeshUploadPlan legacy,compact;NorthlightWorldMesh::PreparationCache preparation;preparation.setLimit(8u*1024u*1024u);
    assert(NorthlightShadowTerrain::build(local,far,center,legacy,error,{},radius,{},false,nullptr,nullptr,&point));
    const auto* retainedVertices=local.vertices.data();const auto retainedTriangles=local.triangles.size();bool trimmed=false;
    assert(NorthlightShadowTerrain::build(local,far,center,compact,error,[&](uint64_t){
        if(!trimmed){localCache.reset();trimmed=true;}
        assert(local.vertices.data()==retainedVertices&&local.triangles.size()==retainedTriangles);return true;
    },radius,{},true,&localBvh,&preparation,&point));
    assert(trimmed&&localCache.stats().retainedBytes==0);
    assert(!compact.source&&!compact.ownedSource&&compact.pagingVertices);
    assert(legacy.indices==compact.indices&&legacy.fixedTerrainChunks==compact.fixedTerrainChunks);
    assert(legacy.vertexCount==compact.vertexCount&&legacy.triangleCount==compact.triangleCount);
    assert(legacy.materials.size()==compact.materials.size());
    for(size_t i=0;i<legacy.materials.size();++i){
        const auto&a=legacy.materials[i];const auto&b=compact.materials[i];
        assert(a.width==b.width&&a.height==b.height&&a.bgra==b.bgra&&a.alphaCutoff==b.alphaCutoff&&a.terrain==b.terrain);
    }
    assert(!std::memcmp(legacy.source->vertices.data(),compact.pagingVertices->data(),size_t(legacy.vertexBytes)));
    const uint64_t saved=legacy.cpuBytes()-compact.cpuBytes();
    assert(saved>=legacy.source->triangles.capacity()*sizeof(WorldTriangle));
    const auto triangles=compact.triangleCount;
    namespace P=NorthlightWorldMeshPages;
    P::Plan reference;assert(P::build(legacy,reference,error));
    auto pages=std::make_shared<P::Plan>();assert(P::build(compact,*pages,error));
    assert(reference.pages.size()==pages->pages.size()&&reference.batches.size()==pages->batches.size());
    for(size_t i=0;i<reference.pages.size();++i){
        const auto&a=reference.pages[i];const auto&b=pages->pages[i];
        assert(a.key==b.key&&a.indices==b.indices&&a.vertices.size()==b.vertices.size());
        assert(!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(WorldVertex)));
    }
    NorthlightWorldMesh::WorldMeshUploadPlan segmented;
    unsigned refused=0;auto limit=[&](uint64_t n){if(n>8ull*1024*1024){++refused;return false;}return true;};
    assert(NorthlightShadowTerrain::build(local,far,center,segmented,error,limit,radius,{},true,&localBvh,&preparation,&point));
    assert(refused&&!segmented.source&&!segmented.pagingVertices&&segmented.pagingChunks&&segmented.pagingIndices);
    assert(segmented.pagingIndexCount()==legacy.indices.size()&&segmented.fixedTerrainChunks==legacy.fixedTerrainChunks);
    for(size_t i=0;i<legacy.indices.size();++i)assert(segmented.pagingIndex(i)==legacy.indices[i]);
    assert(segmented.vertexCount==legacy.vertexCount&&segmented.triangleCount==legacy.triangleCount);
    for(size_t i=0;i<segmented.vertexCount;++i)assert(!std::memcmp(&legacy.source->vertices[i],&segmented.pagingVertex(i),sizeof(WorldVertex)));
    auto chunkPages=std::make_shared<P::Plan>();assert(P::build(segmented,*chunkPages,error,limit));
    assert(reference.pages.size()==chunkPages->pages.size()&&reference.batches.size()==chunkPages->batches.size());
    for(size_t i=0;i<reference.pages.size();++i){const auto&a=reference.pages[i];const auto&b=chunkPages->pages[i];assert(a.key==b.key&&a.indices==b.indices&&a.vertices.size()==b.vertices.size());assert(!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(WorldVertex)));}
    for(size_t i=0;i<reference.batches.size();++i){const auto&a=reference.batches[i];const auto&b=chunkPages->batches[i];assert(a.pointOnly==b.pointOnly&&a.lampSourceId==b.lampSourceId&&a.material==b.material&&a.start==b.start&&a.count==b.count);}
#ifndef FRAGMENTED_STAND
    NorthlightWorldMesh::WorldMeshUploadPlan late;bool deniedKeys=false;const uint64_t keysBytes=uint64_t(legacy.triangleCount)*sizeof(uint32_t);
    auto lateLimit=[&](uint64_t n){if(n==keysBytes&&!deniedKeys){deniedKeys=true;return false;}return true;};
    assert(NorthlightShadowTerrain::build(local,far,center,late,error,lateLimit,radius,{},true,nullptr,nullptr,&point));
    assert(deniedKeys&&late.pagingChunks&&late.pagingIndices&&late.pagingIndexCount()==legacy.indices.size());
    auto latePages=std::make_shared<P::Plan>();assert(P::build(late,*latePages,error));assert(latePages->pages.size()==reference.pages.size());
    for(size_t i=0;i<reference.pages.size();++i)assert(reference.pages[i].key==latePages->pages[i].key);
#endif
    NorthlightMemoryFootprint::Counter footprint;segmented.accountMemory(footprint);assert(footprint.complete()&&footprint.bytes(NorthlightMemoryFootprint::Upload)>=segmented.vertexBytes+segmented.indexBytes);
    auto broken=segmented;broken.pagingIndices.reset();P::Plan unchanged;assert(!P::build(broken,unchanged,error));assert(!P::seal(broken,chunkPages,error));
    const auto kept=segmented.pagingChunks;auto denyBlock=[&](uint64_t n){return n<=8ull*1024*1024&&n!=1024u*1024u;};
    assert(!NorthlightShadowTerrain::build(local,far,center,segmented,error,denyBlock,radius,{},true,nullptr,nullptr,&point));assert(segmented.pagingChunks==kept);
    bool armed=false;auto failBlock=[&](uint64_t n){if(n>8ull*1024*1024)return false;if(n==1024u*1024u&&!armed){armed=true;failNextAllocation=true;}return true;};
    assert(!NorthlightShadowTerrain::build(local,far,center,segmented,error,failBlock,radius,{},true,nullptr,nullptr,&point));assert(armed&&!failNextAllocation&&error=="Geometry allocation deferred"&&segmented.pagingChunks==kept);
    assert(P::seal(segmented,chunkPages,error,limit)&&!P::validateSealed(segmented)&&!segmented.pagingChunks&&!segmented.pagingIndices);
    std::printf("PASS segmented shadow scene capMiB=8 refused=%u exactPages=1 blockFailurePreservesOutput=1\n",refused);
    assert(P::seal(compact,pages,error)&&!P::validateSealed(compact));
    assert(local.triangles.size()>1000000&&saved>30u*1024u*1024u);
    std::printf("PASS Dalaran localTriangles=%zu shadowTriangles=%u pages=%zu freedBeforePagingMiB=%.3f optionalCacheMiB=%.3f exactGeometry=1 exactAlpha=1 retainedGI=1 reserveChanged=0\n",
        local.triangles.size(),triangles,pages->pages.size(),double(saved)/1048576,double(cached)/1048576);
}
