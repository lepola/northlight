#pragma once
#include "world_gi.h"
#include "shadow_terrain.h"
#include "world_mesh_pages.h"
#include "regional_shadow_range.h"
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The real screenshot region, built entirely through production CPU APIs.
// Caller owns address-space pressure, sampling, driver baseline and GPU upload.
namespace NorthlightScenePressureFixture {
struct Result {
    std::shared_ptr<NorthlightGI::BVH> bvh;
    std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> plan;
};
inline Result build(const std::string& root,const NorthlightGI::AllocationAdmission& admit={},
                    NorthlightGI::Vec3 center={5139.84f,-56.93f,380.31f},const std::function<void(const char*)>& profile={},const std::string& map="Northrend"){
    using NorthlightGI::Vec3;
    constexpr double zero=17066.666666666666,tile=533.3333333333333;
    constexpr float radius=928;
    const std::string models=root+"/models";
    const int tx=int(std::floor((zero-center.y)/tile)),ty=int(std::floor((zero-center.x)/tile));
    std::vector<std::string> nearFiles,farFiles;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
        const auto path=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        if(!std::filesystem::exists(path))continue;
        if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(path);
        if(NorthlightRegionalShadow::selected(center,{float(zero-(y+1)*tile),float(zero-(x+1)*tile),-100000},
                                                   {float(zero-y*tile),float(zero-x*tile),100000}))farFiles.push_back(path);
    }
    if(nearFiles.empty()||farFiles.empty())throw std::runtime_error("Scene pressure fixture cache is incomplete");
    std::string error;
    auto check=[&](bool ok,const char* stage){if(!ok)throw std::runtime_error(std::string("Scene pressure fixture ")+stage+": "+error);if(profile)profile(stage);};
    Result result{std::make_shared<NorthlightGI::BVH>(),std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>()};
    NorthlightGI::LocalSceneCache local(NorthlightGI::LocalSceneCache::Limits{0});
    check(local.build(nearFiles,models,map,center-Vec3(288,288,320),center+Vec3(288,288,320),
                      *result.bvh,error,admit,true,{}, {},true),"local");
    result.bvh->shareMaterialTexels();
    NorthlightGI::WorldScene terrain;NorthlightGI::SceneSizeHint hint;
    const auto instance=[center](Vec3 low,Vec3 high){return NorthlightRegionalShadow::selected(center,low,high);};
    const auto chunk=[center](Vec3 low,Vec3 high){return NorthlightRegionalShadow::selectedChunk(center,low,high);};
    check(NorthlightGI::loadInstancedScenes(farFiles,models,center-Vec3(radius,radius,radius),center+Vec3(radius,radius,radius),
          terrain,error,1,admit,instance,chunk,&hint,{},true,true,true),"terrain");
    check(NorthlightShadowTerrain::buildConsuming(result.bvh->scene(),terrain,center,*result.plan,error,admit,radius,{},result.bvh.get(),true),"shadow plan");
    auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();NorthlightWorldMeshPages::Options options;options.spooled=true;
    check(NorthlightWorldMeshPages::build(*result.plan,*pages,error,admit,options),"pages");
    check(NorthlightWorldMeshPages::seal(*result.plan,pages,error,admit),"seal");
    if(const char* invalid=NorthlightWorldMeshPages::validateSealed(*result.plan))throw std::runtime_error(invalid);
    return result;
}
}
