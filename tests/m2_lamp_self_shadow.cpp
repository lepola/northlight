#include "wmo_lamp_shadow.h"
#include "world_mesh_plan.h"
#include "world_mesh_pages.h"
#include "static_shadow_scene.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <filesystem>
using namespace NorthlightGI;
static void check(const WorldScene& scene,const NorthlightLocalLights::Light& light){
    NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;
    assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));
    const auto own=NorthlightWmoLampShadow::parts(light,plan.batches);assert(!own.empty());
    const auto& shell=plan.batches[own.front()];assert(shell.lampTriangles&&shell.lampTriangles->size()==shell.count);
    auto proof=*shell.lampTriangles;assert(NorthlightWmoLampShadow::covers(proof,proof));
    std::reverse(proof.begin(),proof.end());for(auto& t:proof)std::swap(t[0],t[2]);
    assert(NorthlightWmoLampShadow::covers(*shell.lampTriangles,proof));
    proof.resize(1);assert(NorthlightWmoLampShadow::covers(*shell.lampTriangles,proof));
    proof[0][0].x+=.02f;assert(!NorthlightWmoLampShadow::covers(*shell.lampTriangles,proof));
    auto mixed=*shell.lampTriangles;mixed.push_back(proof[0]);
    const auto matched=NorthlightWmoLampShadow::matching(*shell.lampTriangles,mixed);
    assert(matched.size()==mixed.size()&&!matched.back());
    assert(std::count(matched.begin(),matched.end(),true)==int(shell.count));
    mixed.back()=shell.lampTriangles->front();assert(NorthlightWmoLampShadow::matching(*shell.lampTriangles,mixed).empty());
    auto foreign=light;foreign.sourceId^=1;assert(NorthlightWmoLampShadow::parts(foreign,plan.batches).empty());
    auto duplicate=plan.batches;duplicate.push_back(shell);assert(NorthlightWmoLampShadow::parts(light,duplicate).empty());
    NorthlightWorldMeshPages::Plan pages;assert(NorthlightWorldMeshPages::build(plan,pages,error));
    const auto page=NorthlightWmoLampShadow::parts(light,pages.batches);assert(page.size()==own.size());
    for(size_t i=0;i<own.size();++i)assert(pages.batches[page[i]].lampTriangles==plan.batches[own[i]].lampTriangles);
    size_t frame=0;for(const auto& b:plan.batches)if(!b.lampSourceId)frame+=b.count;assert(frame>0);
}
int main(int argc,char** argv){
    assert(argc==2);std::string root=argv[1],error;
    std::vector<NorthlightLocalLights::Light> lights;assert(NorthlightLocalLights::readFile(root+"/sources.fgl",lights)&&lights.size()==1);
    const Vec3 low{-100000,-100000,-100000},high{100000,100000,100000};
    WorldScene scene;assert(loadInstancedScenes({root+"/tile.fg3"},root+"/models",low,high,scene,error));check(scene,lights[0]);
    // The independent directional loader must decode the tag while retaining
    // the complete opaque enclosure for sun, moon and other light sources.
    std::filesystem::create_directories(root+"/Lamp");
    std::filesystem::copy_file(root+"/tile.fg3",root+"/Lamp/30_48.fg3",std::filesystem::copy_options::overwrite_existing);
    {StaticShadow::Streamer stream(root);StaticShadow::Request request;request.map="Lamp";request.center={lights[0].position[0],lights[0].position[1],lights[0].position[2]};
     stream.request(request);assert(stream.waitIdle());auto snapshot=stream.snapshot();assert(snapshot&&snapshot->models.size()==1);
     const auto& model=*snapshot->models.begin()->second;assert(model.indexCount()==scene.triangles.size()*3);
     for(const auto& material:model.materials)assert(material.alphaCutoff>=0.f);}
    LocalSceneCache cache;BVH incremental;assert(cache.build({root+"/tile.fg3"},root+"/models","Azeroth",low,high,incremental,error));check(incremental.scene(),lights[0]);
    BVH reused;assert(cache.build({root+"/tile.fg3"},root+"/models","Azeroth",low,high,reused,error));check(reused.scene(),lights[0]);
    WorldScene filtered=scene;filtered.triangles.clear();size_t shellCount=0;
    for(const auto& t:scene.triangles){if(scene.materials[t.material].lampSourceId==lights[0].sourceId)++shellCount;else filtered.triangles.push_back(t);}
    BVH full,point;assert(full.build(std::move(scene),error,true)&&point.build(std::move(filtered),error,true));
    const auto& l=lights[0];const Vec3 origin{l.position[0],l.position[1],l.position[2]};unsigned before=0,after=0,opened=0;
    for(unsigned i=0;i<4096;++i){const float z=1.f-2.f*(i+.5f)/4096,r=std::sqrt(1-z*z),phi=i*2.39996323f;const Vec3 dir{r*std::cos(phi),r*std::sin(phi),z};
        const auto a=full.trace(origin,dir,.01f,8.f),b=point.trace(origin,dir,.01f,8.f);before+=a.hit;after+=b.hit;opened+=a.hit&&!b.hit;
        if(b.hit)assert(point.scene().materials[point.scene().triangles[b.triangle].material].lampSourceId==0);
    }
    assert(before>after&&opened>500&&after>100&&shellCount>0);
    std::printf("M2_LAMP_PASS %s shell=%zu frame=%zu rays=4096 before=%u after=%u opened=%u\n",argv[1],shellCount,point.scene().triangles.size(),before,after,opened);
}
