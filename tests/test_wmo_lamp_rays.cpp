#include "wmo_lamp_shadow.h"
#include "world_mesh_plan.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace NorthlightGI;
static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d,unsigned material){
    unsigned n=unsigned(s.vertices.size());for(Vec3 p:{a,b,c,d})s.vertices.push_back({p,{0,0,1},0,0});
    s.triangles.push_back({n,n+1,n+2,material});s.triangles.push_back({n,n+2,n+3,material});
}
static void glass(WorldScene& s,float x,float y,unsigned material){
    quad(s,{x-.5f,y-.5f,-.5f},{x+.5f,y-.5f,-.5f},{x+.5f,y-.5f,.5f},{x-.5f,y-.5f,.5f},material);
    quad(s,{x+.5f,y-.5f,-.5f},{x+.5f,y+.5f,-.5f},{x+.5f,y+.5f,.5f},{x+.5f,y-.5f,.5f},material);
    quad(s,{x+.5f,y+.5f,-.5f},{x-.5f,y+.5f,-.5f},{x-.5f,y+.5f,.5f},{x+.5f,y+.5f,.5f},material);
    quad(s,{x-.5f,y+.5f,-.5f},{x-.5f,y-.5f,-.5f},{x-.5f,y-.5f,.5f},{x-.5f,y+.5f,.5f},material);
}
int main(){WorldScene scene;scene.materials.resize(5);scene.materials[0].lampSourceId=1;scene.materials[3].lampSourceId=2;
    glass(scene,0,0,0);glass(scene,4,4,3);
    quad(scene,{3,-1,-1},{3,1,-1},{3,1,1},{3,-1,1},1); // wall
    quad(scene,{-1,2,-1},{1,2,-1},{1,2,1},{-1,2,1},1); // metal
    quad(scene,{-2,-1,-1},{-2,1,-1},{-2,1,1},{-2,-1,1},2); // unrelated glass
    quad(scene,{-1,-1,-2},{1,-1,-2},{1,1,-2},{-1,1,-2},4); // table
    NorthlightLocalLights::Light light{};light.sourceId=1;light.diffuse[0]=.3f;light.attenuationStart=.8f;light.attenuationEnd=5.f;light.kind=3;light.flags=NorthlightLocalLights::CastShadow|NorthlightLocalLights::GlassEmitter;
    NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));
    const size_t own=NorthlightWmoLampShadow::unique(light,plan.batches);assert(own!=size_t(-1)&&plan.batches[own].material==0&&plan.batches[own].count==8);
    WorldScene filtered=scene;std::vector<WorldTriangle> keep;for(const auto& t:scene.triangles)if(t.material!=plan.batches[own].material)keep.push_back(t);filtered.triangles=std::move(keep);
    for(bool fast:{false,true}){BVH full,point;WorldScene copy=scene,limited=filtered;assert(full.build(std::move(copy),error,fast)&&point.build(std::move(limited),error,fast));
        const Vec3 origin{0,0,0};assert(full.occluded(origin,{1,0,0},1,.01f));assert(!point.occluded(origin,{1,0,0},1,.01f));
        assert(point.occluded(origin,{1,0,0},4,.01f));assert(point.occluded(origin,{0,1,0},3,.01f));assert(point.occluded(origin,{-1,0,0},3,.01f));assert(point.occluded(origin,{0,0,-1},3,.01f));
        assert(!point.occluded(origin,{0,-1,0},3,.01f));assert(point.occluded({4,4,0},{1,0,0},1,.01f));
    }
    std::puts("PASS own enclosure transmits; wall, metal, table and other glass still occlude");
}
