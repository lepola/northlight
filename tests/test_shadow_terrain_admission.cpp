#include "shadow_terrain.h"
#include "geometry_memory.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
using namespace NorthlightGI;
int main(int argc,char**argv){
    assert(argc==2);std::string root=argv[1];
    using namespace NorthlightGeometryMemory;
    const Sample fragmented{2195*MiB,90*MiB,true};
    assert(!admits(fragmented,buildBudget(128*MiB))); // observed 0.3.92 deadlock
    // Bael Modan 0.3.188 sample: extended-reach growth checks are refused (see test_terrain_reach_fallback).
    const Sample baelModan{760*MiB,63*MiB,true};
    for(double mib:{1.0,23.58,37.92,64.0})assert(!admits(baelModan,buildBudget(uint64_t(mib*MiB))));
    for(Vec3 center:{Vec3(-10875.52f,-761.07f,61.47f),Vec3(-10726.54f,-1214.74f,35.38f)}){
        int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));
        std::vector<std::string> localFiles,farFiles;
        for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){auto p=root+"/Azeroth/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(std::filesystem::exists(p)){farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)localFiles.push_back(p);}}
        assert(!localFiles.empty());WorldScene local,far;std::string error;
        assert(loadInstancedScenes(localFiles,root+"/models",center-Vec3(288,288,320),center+Vec3(288,288,320),local,error));
        uint64_t largest=0;unsigned checks=0;
        AllocationAdmission guard=[&](uint64_t bytes){largest=std::max(largest,bytes);++checks;return admits(fragmented,buildBudget(bytes));};
        constexpr float reach=NorthlightShadowTerrain::Radius;
        assert(loadInstancedScenes(farFiles,root+"/models",center-Vec3(reach,reach,reach),center+Vec3(reach,reach,reach),far,error,1,guard));
        NorthlightWorldMesh::WorldMeshUploadPlan plan,reference;
        assert(NorthlightShadowTerrain::build(local,far,center,plan,error,guard));
        assert(NorthlightShadowTerrain::build(local,far,center,reference,error));
        assert(plan.indices==reference.indices&&plan.fixedTerrainChunks==reference.fixedTerrainChunks);
        const auto& a=*plan.source;const auto& b=*reference.source;
        assert(a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size());
        assert(!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(WorldVertex)));
        assert(!std::memcmp(a.triangles.data(),b.triangles.data(),a.triangles.size()*sizeof(WorldTriangle)));
        for(size_t i=0;i<a.materials.size();++i){assert(a.materials[i].rgba==b.materials[i].rgba);assert(a.materials[i].alphaCutoff==b.materials[i].alphaCutoff);}
        auto previous=plan.source;
        assert(!NorthlightShadowTerrain::build(local,far,center,plan,error,[](uint64_t){return false;}));
        assert(plan.source==previous); // refusal never replaces a valid plan
        WorldScene unchanged;unchanged.vertices.push_back({{1,2,3},{},0,0});
        assert(!loadInstancedScenes(farFiles,root+"/models",center-Vec3(reach,reach,reach),center+Vec3(reach,reach,reach),unchanged,error,1,[](uint64_t){return false;}));
        assert(unchanged.vertices.size()==1&&unchanged.vertices[0].position.x==1);
        std::cout<<"Duskwood "<<center.x<<","<<center.y<<" triangles="<<plan.triangleCount<<" largestMiB="<<double(largest)/MiB<<" guardedChecks="<<checks<<" fragmented90MiB=PASS denialPreservesOutput=PASS\n";
    }
}
