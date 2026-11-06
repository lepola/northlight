#include "shadow_terrain.h"
#include "point_light_shadow.h"
#include "local_light_selection.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace NorthlightGI;
int main(int argc,char** argv){
    assert(argc==3);std::string error;
    WorldScene point,wrong;
    const std::vector<std::string> tiles={argv[1]};
    const bool wrongLoaded=loadInstancedScenes(tiles,argv[2],{-80,-80,-80},{80,80,80},wrong,error,1);
    assert(!wrongLoaded||wrong.triangles.empty());error.clear();
    assert(loadInstancedScenes(tiles,argv[2],{-80,-80,-80},{80,80,80},point,error,10));
    assert(point.triangles.size()==2&&point.vertices.size()==4&&point.materials.size()==1);
    unsigned covered=0;for(size_t i=3;i<point.materials[0].rgba.size();i+=4)covered+=point.materials[0].rgba[i]>=128;
    assert(covered==20&&point.materials[0].rgba.size()==256);
    assert(!point.materials[0].pointOnly); // FGS2 is unchanged; routing is an explicit runtime decision.
    WorldScene local,terrain;local.materials.emplace_back();
    local.vertices={{{-.3f,-.3f,0},{0,0,1},0,0},{{.3f,-.3f,0},{0,0,1},1,0},{{0,.3f,0},{0,0,1},.5f,1}};
    local.triangles={{0,1,2,0}};
    NorthlightWorldMesh::WorldMeshUploadPlan before,after;
    assert(NorthlightShadowTerrain::build(local,terrain,{},before,error));
    assert(NorthlightShadowTerrain::build(local,terrain,{},after,error,{},NorthlightShadowTerrain::Radius,{},false,nullptr,nullptr,&point));
    assert(local.triangles.size()==1&&local.materials.size()==1&&point.triangles.size()==2);
    assert(before.triangleCount==1&&after.triangleCount==3);
    unsigned ordinary=0,pointTriangles=0;
    for(const auto& batch:after.batches){if(batch.pointOnly)pointTriangles+=batch.count;else ordinary+=batch.count;}
    assert(ordinary==1&&pointTriangles==2);
    assert(after.localShadowRecords&&before.localShadowRecords);
    const float matrix[16]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0,0,0,0,1};
    const std::set<std::pair<int,int>> fixed;
    assert(NorthlightLocalShadowSignature::signature(*before.localShadowRecords,fixed,matrix)==
           NorthlightLocalShadowSignature::signature(*after.localShadowRecords,fixed,matrix));
    unsigned pointRecords=0;for(const auto& record:*after.localShadowRecords)pointRecords+=record.pointOnly;
    assert(pointRecords&&after.materials.back().alphaCutoff==.5f);
    auto broken=point;broken.triangles[0].v0=999;
    assert(!NorthlightShadowTerrain::build(local,terrain,{},after,error,{},NorthlightShadowTerrain::Radius,{},false,nullptr,nullptr,&broken));
    assert(after.triangleCount==3); // Failed optional assembly is transactional.
    uint64_t baseMaximum=0;
    assert(NorthlightShadowTerrain::build(local,terrain,{},before,error,[&](uint64_t bytes){baseMaximum=std::max(baseMaximum,bytes);return true;}));
    assert(baseMaximum);
    NorthlightGI::AllocationAdmission limited=[&](uint64_t bytes){return bytes<=baseMaximum;};
    auto budgetPoint=point;budgetPoint.vertices.resize(baseMaximum/sizeof(WorldVertex)+8);
    NorthlightWorldMesh::WorldMeshUploadPlan fallback;
    assert(!NorthlightShadowTerrain::build(local,terrain,{},fallback,error,limited,NorthlightShadowTerrain::Radius,{},false,nullptr,nullptr,&budgetPoint));
    assert(NorthlightShadowTerrain::build(local,terrain,{},fallback,error,limited));
    assert(fallback.triangleCount==1); // Optional transmission never requires lowering the base geometry budget.
    auto light=NorthlightLocalLights::Light{{0,0,2},{.1f,.04f,.01f},.5f,5,12345,3,
        NorthlightLocalLights::CastShadow|NorthlightLocalLights::FirePulse};
    assert(NorthlightLocalLights::valid(light));auto invalid=light;invalid.flags|=32;assert(!NorthlightLocalLights::valid(invalid));
    invalid=light;invalid.flags|=NorthlightLocalLights::CandlePulse;assert(!NorthlightLocalLights::valid(invalid));
    double sum=0;const uint32_t period=1240+uint32_t(light.sourceId%503);
    for(uint32_t time=0;time<period*3;++time){const float gain=NorthlightLocalLights::pulseGain(light,time);assert(gain>.9f&&gain<1.1f);sum+=gain;}
    assert(std::fabs(sum/(period*3)-1)<1e-6);
    NorthlightLocalLightSelection::Tracker tracker;std::vector<NorthlightLocalLights::Light> lights={light};float camera[3]={};
    for(uint32_t time:{0u,100u,500u,1500u}){
        const auto selected=tracker.update(lights,camera,32,.1f,false,nullptr,nullptr,0,nullptr,0,time);
        assert(selected.count==1&&selected.sources[0].sourceId==light.sourceId);
        for(unsigned k=0;k<3;++k){const float expected=light.diffuse[k]*NorthlightLocalLights::pulseGain(light,time);
            assert(selected.color[0][k]==expected&&selected.sources[0].diffuse[k]==expected);}
    }
    NorthlightPointShadow::RefreshSchedule schedule;schedule.commit(100,light,1);auto pulsed=light;
    for(auto& c:pulsed.diffuse)c*=1.05f;
    assert(!schedule.due(101,pulsed,1,300,false)); // Colour flicker never invalidates the static cube.
    std::puts("PASS point-only loader/category, neutral transmission, unchanged directional/GI sources, transactional failure, bounded mean-preserving pulses and matching shadow colour");
}
