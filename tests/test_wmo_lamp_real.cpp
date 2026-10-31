#include "wmo_lamp_shadow.h"
#include "world_mesh_plan.h"
#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include <xmmintrin.h>
static void checkTags(const char* path){
    std::ifstream file(path,std::ios::binary);assert(file);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});assert(bytes.size()>=20);
    uint32_t nv,nt;std::memcpy(&nv,bytes.data()+8,4);std::memcpy(&nt,bytes.data()+12,4);
    const size_t offset=20+size_t(nv)*32+size_t(nt)*16+20;assert(offset+4<=bytes.size());
    const std::string changed=std::string(path)+".tag.fgs";const unsigned saved=_mm_getcsr();
    for(unsigned mode:{0u,0x8040u}){
        _mm_setcsr((saved&~0x8040u)|mode);
        for(float tag:{-1.f,-8388607.f,-1.5f,-8388608.f,-.5f,-std::numeric_limits<float>::denorm_min(),-0.f}){
            std::memcpy(bytes.data()+offset,&tag,4);
            {std::ofstream output(changed,std::ios::binary);output.write(bytes.data(),bytes.size());assert(output);}
            NorthlightGI::WorldScene scene;std::string error;const bool valid=tag==-1.f||tag==-8388607.f;
            uint32_t bits;std::memcpy(&bits,&tag,4);const bool zero=(bits&0x7fffffffu)==0;
            assert(NorthlightGI::loadScene(changed.c_str(),scene,error)==(valid||zero));
            if(valid)assert(scene.materials[0].lampLocalId==uint32_t(-tag)&&scene.materials[0].alphaCutoff==0.f);
            if(zero)assert(scene.materials[0].lampLocalId==0);
        }
    }
    _mm_setcsr(saved);
}
static void checkMatches(const NorthlightGI::WorldScene& scene,const std::vector<NorthlightLocalLights::Light>& lights){
    NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));
    std::vector<size_t> matched;for(const auto& light:lights){const size_t index=NorthlightWmoLampShadow::unique(light,plan.batches);assert(index!=size_t(-1));matched.push_back(index);}
    for(size_t i=0;i<matched.size();++i)for(size_t j=0;j<i;++j)assert(matched[i]!=matched[j]);
    for(unsigned anchor:{0u,NorthlightWorldMeshPages::AnchorModulus}){
        NorthlightWorldMeshPages::Options options;options.anchorModulus=anchor;
        NorthlightWorldMeshPages::Plan pages;assert(NorthlightWorldMeshPages::build(plan,pages,error,{},options));
        for(const auto& light:lights)assert(NorthlightWmoLampShadow::unique(light,pages.batches)!=size_t(-1));
        NorthlightMeshPageKey::KeySet resident;for(const auto& page:pages.pages)resident.insert(page.key);
        options.resident=&resident;NorthlightWorldMeshPages::Plan reused;assert(NorthlightWorldMeshPages::build(plan,reused,error,{},options));
        for(const auto& page:reused.pages)assert(page.elided);
        for(const auto& light:lights)assert(NorthlightWmoLampShadow::unique(light,reused.batches)!=size_t(-1));
    }
}
int main(int argc,char** argv){assert(argc==6);NorthlightGI::WorldScene scene;std::string error;assert(NorthlightGI::loadScene(argv[1],scene,error));
    checkTags(argv[1]);
    std::vector<NorthlightLocalLights::Light> lights;assert(NorthlightLocalLights::readFile(argv[2],lights)&&lights.size()==27);
    NorthlightWorldMesh::WorldMeshUploadPlan flat;assert(NorthlightWorldMesh::buildUploadPlan(scene,flat,error));for(const auto& light:lights)assert(NorthlightWmoLampShadow::unique(light,flat.batches)==size_t(-1));
    const NorthlightGI::Vec3 low{-100000,-100000,-100000},high{100000,100000,100000};const std::vector<std::string> tiles={argv[3]};NorthlightGI::WorldScene instanced;
    assert(NorthlightGI::loadInstancedScenes(tiles,argv[4],low,high,instanced,error));checkMatches(instanced,lights);
    auto native=lights;for(auto& light:native){light.kind=1;light.flags|=NorthlightLocalLights::Baked;light.attenuationStart=0.f;assert(NorthlightLocalLights::valid(light));}checkMatches(instanced,native);
    NorthlightGI::LocalSceneCache cache;NorthlightGI::BVH incremental;assert(cache.build(tiles,argv[4],"Azeroth",low,high,incremental,error));checkMatches(incremental.scene(),lights);
    NorthlightGI::BVH reused;assert(cache.build(tiles,argv[4],"Azeroth",low,high,reused,error)&&cache.stats().pieceReuses>0);checkMatches(reused.scene(),lights);
    NorthlightGI::WorldScene overlap;assert(NorthlightGI::loadInstancedScenes({argv[5]},argv[4],low,high,overlap,error));NorthlightWorldMesh::WorldMeshUploadPlan overlapPlan;assert(NorthlightWorldMesh::buildUploadPlan(overlap,overlapPlan,error));
    const auto ownIndex=NorthlightWmoLampShadow::unique(lights[0],overlapPlan.batches);assert(ownIndex!=size_t(-1));auto otherSource=lights[0];otherSource.sourceId^=1;assert(NorthlightWmoLampShadow::unique(otherSource,overlapPlan.batches)==size_t(-1));
    unsigned coincidentOthers=0;const auto& ownBatch=overlapPlan.batches[ownIndex];for(const auto& candidate:overlapPlan.batches)if(candidate.lampSourceId&&candidate.lampSourceId!=ownBatch.lampSourceId&&candidate.count==8&&candidate.boundsLow.x==ownBatch.boundsLow.x&&candidate.boundsHigh.x==ownBatch.boundsHigh.x&&candidate.boundsLow.y==ownBatch.boundsLow.y&&candidate.boundsHigh.y==ownBatch.boundsHigh.y)++coincidentOthers;assert(coincidentOthers==1);
    scene=std::move(instanced);NorthlightWorldMesh::WorldMeshUploadPlan plan;assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));
    std::vector<size_t> matched;for(const auto& light:lights){const size_t index=NorthlightWmoLampShadow::unique(light,plan.batches);assert(index!=size_t(-1));matched.push_back(index);}
    const auto& source=lights[0];const auto& batch=plan.batches[matched[0]];NorthlightGI::WorldScene filtered=scene;std::vector<NorthlightGI::WorldTriangle> keep;
    for(const auto& triangle:scene.triangles)if(triangle.material!=batch.material)keep.push_back(triangle);assert(keep.size()+8==scene.triangles.size());filtered.triangles=std::move(keep);
    NorthlightGI::BVH full,point;assert(full.build(std::move(scene),error,true)&&point.build(std::move(filtered),error,true));
    const NorthlightGI::Vec3 origin{source.position[0],source.position[1],source.position[2]};unsigned visible=0;
    for(const auto& triangle:full.scene().triangles)if(triangle.material==batch.material){const auto a=full.scene().vertices[triangle.v0].position,b=full.scene().vertices[triangle.v1].position,c=full.scene().vertices[triangle.v2].position;const auto dir=NorthlightGI::normalized((a+b+c)/3.f-origin);
        const auto before=full.trace(origin,dir,.01f,source.attenuationStart*1.2f),after=point.trace(origin,dir,.01f,source.attenuationStart*1.2f);
        if(before.hit&&before.triangle<full.scene().triangles.size()&&full.scene().triangles[before.triangle].material==batch.material){assert(!after.hit||after.distance>=before.distance);++visible;}}
    assert(visible>0);std::printf("PASS 27 real lamp matches; shared geometry retained; %u enclosure rays tested\n",visible);
}
