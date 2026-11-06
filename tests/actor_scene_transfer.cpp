#include <cstdlib>
#include <new>
#include <cstdio>
static size_t allocatedBytes=0,allocations=0;
void* operator new(size_t n){++allocations;allocatedBytes+=n;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_scene_job.h"
#include <cassert>
#include <chrono>
#include <type_traits>
using namespace NorthlightGI;
using NorthlightActorGeometry::Result;
static_assert(!std::is_copy_constructible_v<Result>);
static Result fixture(){
    Result result;result.scene=std::make_unique<WorldScene>();auto& s=*result.scene;
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)s.vertices.push_back({{-9157.f+x*.25f,354.f+y*.25f,93.f+float((x*13+y*7)%17)*.1f},{0,0,1},float(x)/127,float(y)/127});
    for(unsigned y=0;y<127;++y)for(unsigned x=0;x<127;++x){const unsigned a=y*128+x,m=(y*53)/127;s.triangles.push_back({a,a+1,a+128,m});s.triangles.push_back({a+1,a+129,a+128,m});}
    for(unsigned i=0;i<53;++i){WorldMaterial m;m.albedo={.8f,.7f,.6f};m.width=m.height=128;m.alphaCutoff=i%3?.5f:0.f;m.rgba.resize(128*128*4);
        for(size_t j=0;j<m.rgba.size();++j)m.rgba[j]=uint8_t((j*17+i*29)>>(j%7));s.materials.push_back(std::move(m));}
    result.hash=NorthlightActorGeometry::sceneHash(s);return result;
}
struct Measurement {double transferUs=0,totalUs=0;size_t allocations=0,bytes=0;};
static Measurement consume(Result result,bool move,BVH& bvh){
    const auto* vertices=result.scene->vertices.data();const auto* texels=result.scene->materials[0].texels().data();const auto hash=result.hash;
    const auto begin=std::chrono::steady_clock::now();const auto count=allocations,bytes=allocatedBytes;
    auto scene=move?std::move(*result.scene):*result.scene;
    Measurement measured;measured.transferUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();measured.allocations=allocations-count;measured.bytes=allocatedBytes-bytes;
    assert((scene.vertices.data()==vertices)==move);assert((scene.materials[0].texels().data()==texels)==move);
    assert(NorthlightActorGeometry::sceneHash(scene)==hash);std::string error;
    // Keep validation outside the timed section; it intentionally scans all texels.
    const auto buildBegin=std::chrono::steady_clock::now();assert(bvh.build(std::move(scene),error));
    measured.totalUs=measured.transferUs+std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-buildBegin).count();
    assert(NorthlightActorGeometry::sceneHash(bvh.scene())==hash);
    assert((bvh.scene().vertices.data()==vertices)==move);assert((bvh.scene().materials[0].texels().data()==texels)==move);
    return measured;
}
int main(){
    BVH copied,moved;auto original=fixture();const auto hash=original.hash;
    auto copyResult=fixture(),moveResult=fixture();consume(std::move(copyResult),false,copied);consume(std::move(moveResult),true,moved);
    for(unsigned i=0;i<4096;++i){Vec3 origin{-9157.f+float(i%64)*.5f,354.f+float(i/64)*.5f,100.f},direction{.01f,-.02f,-1.f};
        const auto a=copied.trace(origin,direction),b=moved.trace(origin,direction);
        assert(a.hit==b.hit&&a.backFace==b.backFace&&a.distance==b.distance&&a.triangle==b.triangle);
        assert(!std::memcmp(&a.position,&b.position,sizeof a.position)&&!std::memcmp(&a.normal,&b.normal,sizeof a.normal)&&!std::memcmp(&a.geometricNormal,&b.geometricNormal,sizeof a.geometricNormal)&&!std::memcmp(&a.albedo,&b.albedo,sizeof a.albedo));
        assert(copied.occluded(origin,direction)==moved.occluded(origin,direction));}
    assert(NorthlightActorGeometry::sceneHash(*original.scene)==hash);
    Measurement sum[2];constexpr unsigned rounds=20;
    for(unsigned round=0;round<rounds;++round)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(round+turn)&1;auto result=fixture();BVH bvh;const auto m=consume(std::move(result),mode!=0,bvh);sum[mode].transferUs+=m.transferUs;sum[mode].totalUs+=m.totalUs;sum[mode].allocations+=m.allocations;sum[mode].bytes+=m.bytes;}
    assert(sum[0].bytes>3*1024*1024*rounds&&sum[0].allocations>=55*rounds);assert(sum[1].bytes==0&&sum[1].allocations==0);
    std::printf("PASS actor scene transfer: vertices=%zu triangles=%zu materials=53 exactHash=1 exactRays=4096 exclusiveOwner=1 oldTransferUs=%.3f newTransferUs=%.3f oldTotalUs=%.3f newTotalUs=%.3f oldAllocations=%zu newAllocations=%zu oldAllocatedBytes=%zu newAllocatedBytes=%zu\n",original.scene->vertices.size(),original.scene->triangles.size(),sum[0].transferUs/rounds,sum[1].transferUs/rounds,sum[0].totalUs/rounds,sum[1].totalUs/rounds,sum[0].allocations/rounds,sum[1].allocations/rounds,sum[0].bytes/rounds,sum[1].bytes/rounds);
}
