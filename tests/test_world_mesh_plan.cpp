#include "world_mesh_plan.h"
#include "shadow_bounds.h"
#include "world_math.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

using namespace NorthlightGI;
using namespace NorthlightWorldMesh;

static void triangle(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,uint32_t material){
    uint32_t v=uint32_t(s.vertices.size());
    for(Vec3 p:{a,b,c})s.vertices.push_back({p,{0,0,1},0,0});
    s.triangles.push_back({v,v+1,v+2,material});
}
static void chunkTriangle(WorldScene& s,int x,int y,uint32_t material){
    constexpr double origin=17066.666666666666,step=100.0/3.0;
    float mx=float(origin-y*step),my=float(origin-x*step);
    triangle(s,{mx-4,my-4,0},{mx-8,my-4,0},{mx-4,my-8,0},material);
}
static void coherence(const WorldScene& s,const WorldMeshUploadPlan& p){
    assert(p.source==&s&&p.vertexCount==s.vertices.size()&&p.triangleCount==s.triangles.size());
    assert(p.vertexBytes==s.vertices.size()*sizeof(WorldVertex));assert(p.indexBytes==p.indices.size()*4);
    assert(p.indices.size()==s.triangles.size()*3&&p.materials.size()==s.materials.size());
    using Key=std::array<uint32_t,4>;std::map<Key,unsigned> original,repacked;
    for(const auto&t:s.triangles)++original[{t.v0,t.v1,t.v2,t.material}];
    size_t next=0;
    for(const auto& b:p.batches){
        assert(b.start==next&&b.material<s.materials.size()&&b.count>0);
        assert(b.terrain==s.materials[b.material].terrain);
        if(!b.terrain)assert(b.chunkX==-1&&b.chunkY==-1);
        const float inf=std::numeric_limits<float>::infinity();Vec3 low(inf,inf,inf),high(-inf,-inf,-inf);
        for(size_t j=b.start;j<b.start+size_t(b.count)*3;j+=3){
            assert(j+2<p.indices.size());++repacked[{p.indices[j],p.indices[j+1],p.indices[j+2],b.material}];
            for(unsigned k=0;k<3;++k){Vec3 v=s.vertices[p.indices[j+k]].position;
                low.x=std::min(low.x,v.x);low.y=std::min(low.y,v.y);low.z=std::min(low.z,v.z);
                high.x=std::max(high.x,v.x);high.y=std::max(high.y,v.y);high.z=std::max(high.z,v.z);}
        }
        assert(b.boundsLow.x==low.x&&b.boundsLow.y==low.y&&b.boundsLow.z==low.z);
        assert(b.boundsHigh.x==high.x&&b.boundsHigh.y==high.y&&b.boundsHigh.z==high.z);
        next+=size_t(b.count)*3;
    }
    assert(next==p.indices.size()&&original==repacked);
    uint64_t bytes=0;for(const auto&m:p.materials){assert(uint64_t(m.width)*m.height*4==m.bgra.size());bytes+=m.bgra.size();}
    assert(bytes==p.textureBytes);
}
static void testShadowBounds(){
    using NorthlightShadowBounds::clipReject;
    float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    assert(!clipReject({-.5f,-.5f,.2f},{.5f,.5f,.8f},identity));
    assert(!clipReject({-2,-2,-1},{2,2,2},identity)); // Encloses clip volume.
    assert(!clipReject({.5f,-.1f,.2f},{2,.1f,.8f},identity)); // Crosses right plane.
    for(auto pair:{std::pair<Vec3,Vec3>{{-3,-.1f,.2f},{-2,.1f,.8f}},
                  {{2,-.1f,.2f},{3,.1f,.8f}},{{-.1f,-3,.2f},{.1f,-2,.8f}},
                  {{-.1f,2,.2f},{.1f,3,.8f}},{{-.1f,-.1f,-2},{.1f,.1f,-1}},
                  {{-.1f,-.1f,2},{.1f,.1f,3}}})assert(clipReject(pair.first,pair.second,identity));
    assert(!clipReject({1.00001f,0,.5f},{1.00002f,0,.5f},identity)); // Numerical safety margin.
    assert(!clipReject({0,0,0},{0,0,0},identity)); // Plane contact is retained.
    assert(!clipReject({1,0,0},{0,0,0},identity)); // Reversed bound.
    assert(!clipReject({0,0,0},{std::numeric_limits<float>::infinity(),0,0},identity));
    assert(!clipReject({0,0,0},{0,0,0},nullptr));
    float broken[16];std::memcpy(broken,identity,sizeof broken);broken[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!clipReject({5,0,0},{6,0,0},broken));
    std::memcpy(broken,identity,sizeof broken);broken[3]=1;
    assert(!clipReject({5,0,0},{6,0,0},broken)); // Unsupported projection fails open.
    assert(!clipReject({5,0,0},{6,0,0},identity,-1));
    for(float radius:{48.f,192.f}){
        float light[16];NorthlightWorldMath::shadowMatrix({0,0,0},{0,0,1},radius,light);
        assert(!clipReject({-1,-1,-1},{1,1,1},light));
        assert(clipReject({300,0,0},{310,1,1},light));
        // The 0.3.67 mountain fix expanded depth to +/-640. Test the actual
        // current span instead of the old +/-320 boundary.
        assert(!clipReject({0,0,400},{1,1,410},light));
        const float beyond=NorthlightWorldMath::ShadowDepthSpan*.5f+10;
        assert(clipReject({0,0,beyond},{1,1,beyond+10},light));
        assert(!clipReject({radius-1,0,0},{radius+1,1,1},light));
        assert(clipReject({60,0,0},{61,1,1},light)==(radius==48));
    }
    // Large translated/rotated world coordinates: any box containing an
    // interior point must remain visible despite cancellation in GPU floats.
    float light[16];Vec3 center(-10700,-920,60);
    NorthlightWorldMath::shadowMatrix(center,normalized({.64f,.64f,.41f}),48,light);
    assert(!clipReject(center-Vec3(1,1,1),center+Vec3(1,1,1),light));
    std::puts("PASS conservative shadow AABB rejection: six planes, intersections, near/far, numerical margin and invalid fail-open");
}
static void testGrouping(){
    WorldScene s;WorldMaterial terrain;terrain.terrain=true;s.materials.push_back(terrain);
    WorldMaterial cutout;cutout.width=2;cutout.height=1;cutout.rgba={1,2,3,4,5,6,7,8};s.materials.push_back(cutout);
    s.materials.push_back({});WorldMaterial opaque=cutout;opaque.alphaCutoff=0;s.materials.push_back(opaque);
    chunkTriangle(s,100,200,0);chunkTriangle(s,101,200,0);chunkTriangle(s,100,200,1);
    chunkTriangle(s,100,200,0);chunkTriangle(s,102,201,2);chunkTriangle(s,103,202,3);
    // Crosses the global chunk boundary x=0. It must stay unmaskable instead
    // of disappearing whenever the centroid's chunk has a live replay.
    triangle(s,{-1,-5,0},{1,-5,0},{-1,-7,0},0);
    WorldMeshUploadPlan p;std::string error;assert(buildUploadPlan(s,p,error));coherence(s,p);
    unsigned repeated=0,crossing=0;
    for(const auto& b:p.batches){
        if(b.terrain&&b.chunkX==100&&b.chunkY==200){assert(b.count==2);++repeated;}
        if(b.terrain&&b.chunkX==-1){assert(b.chunkY==-1&&b.count==1);++crossing;}
    }
    assert(repeated==1&&crossing==1&&p.unmaskableTerrainTriangles==1);
    assert((p.materials[1].bgra==std::vector<uint8_t>{3,2,1,4,7,6,5,8}));
    assert(p.materials[1].width==2&&p.materials[1].height==1);
    assert(p.materials[3].width==1&&p.materials[3].height==1);
    assert((p.materials[3].bgra==std::vector<uint8_t>{255,255,255,255}));
    assert(p.materials[0].terrain&&!p.materials[1].terrain);
    auto first=p.indices;WorldMeshUploadPlan again;assert(buildUploadPlan(s,again,error));assert(first==again.indices);
    std::puts("PASS exact terrain chunk batches, unmaskable crossings, material-preserving index packing and opaque/cutout texture preparation");
}
static void testFailure(){
    WorldScene s;s.materials.push_back({});triangle(s,{0,0,0},{1,0,0},{0,1,0},0);
    WorldMeshUploadPlan plan;std::string error;assert(buildUploadPlan(s,plan,error));auto before=plan.indices;auto source=plan.source;
    WorldScene broken=s;broken.triangles[0].v0=999;
    assert(!buildUploadPlan(broken,plan,error));assert(!error.empty()&&plan.indices==before&&plan.source==source);
    broken=s;broken.materials[0].width=2;broken.materials[0].height=2;
    assert(!buildUploadPlan(broken,plan,error));assert(plan.indices==before);
    broken=s;broken.vertices[0].position.z=std::numeric_limits<float>::quiet_NaN();
    assert(!buildUploadPlan(broken,plan,error));assert(plan.indices==before);
    WorldScene empty;assert(buildUploadPlan(empty,plan,error));coherence(empty,plan);assert(plan.batches.empty());
    std::puts("PASS malformed source rejection, unchanged prior plan and empty scene handling");
}
static void testTextureSource(){
    // Metadata-only scene materials + an external texel source must produce the plan the full scene does.
    WorldScene full;full.materials.resize(3);
    full.materials[0].alphaCutoff=.5f;full.materials[0].width=2;full.materials[0].height=1;full.materials[0].rgba={1,2,3,4,5,6,7,8};
    full.materials[1].alphaCutoff=0;full.materials[1].width=1;full.materials[1].height=1;full.materials[1].rgba={9,9,9,9};
    full.materials[2].terrain=true;
    for(uint32_t m=0;m<3;++m)triangle(full,{float(m),0,0},{float(m)+1,0,0},{float(m),1,0},m);
    WorldScene meta=full;std::vector<WorldMaterial> textures=full.materials;
    for(auto& m:meta.materials){m.rgba.clear();m.width=m.height=0;}
    textures.resize(2); // material 2 keeps its own (empty) data
    WorldMeshUploadPlan a,b;std::string error;
    assert(buildUploadPlan(full,a,error));assert(buildUploadPlan(meta,b,error,{},&textures));
    assert(a.indices==b.indices&&a.batches.size()==b.batches.size()&&a.materials.size()==b.materials.size());
    for(size_t i=0;i<a.materials.size();++i)
        assert(a.materials[i].bgra==b.materials[i].bgra&&a.materials[i].width==b.materials[i].width&&a.materials[i].height==b.materials[i].height&&a.materials[i].alphaCutoff==b.materials[i].alphaCutoff&&a.materials[i].terrain==b.materials[i].terrain);
    assert(a.localShadowRecords->size()==b.localShadowRecords->size());
    for(size_t i=0;i<a.localShadowRecords->size();++i)assert((*a.localShadowRecords)[i].content==(*b.localShadowRecords)[i].content);
    // Without the source the metadata-only scene has a cutout material with no texels: width 0, so it is an opaque 1x1.
    WorldMeshUploadPlan c;assert(buildUploadPlan(meta,c,error));assert((c.materials[0].bgra==std::vector<uint8_t>{255,255,255,255}));
    // A source longer than the scene's materials, or with inconsistent dimensions, is rejected.
    std::vector<WorldMaterial> longer(4);assert(!buildUploadPlan(meta,c,error,{},&longer));
    textures[0].width=3;assert(!buildUploadPlan(meta,c,error,{},&textures));
    std::puts("PASS external texture source builds the identical plan without texels in the scene");
}
static void testSharedBatchFlags(){
    WorldScene scene;scene.materials.resize(3);
    scene.materials[1].pointOnly=true;
    scene.materials[2].lampSourceId=0x123456789abcdef0ull;
    for(unsigned i=0;i<6;++i)triangle(scene,{float(i),0,0},{float(i)+1,0,0},{float(i),1,0},i%3);
    std::vector<PreparationPiece> pieces(2);
    for(unsigned i=0;i<2;++i){pieces[i].identity=std::to_string(i);pieces[i].firstTriangle=i*3;pieces[i].triangleCount=3;pieces[i].materials={0,1,2};}
    PreparationCache cache;cache.setLimit(8*1024*1024);
    WorldMeshUploadPlan reference;std::string error;assert(buildUploadPlan(scene,reference,error));
    for(unsigned pass=0;pass<2;++pass){
        WorldMeshUploadPlan shared;assert(buildUploadPlan(scene,shared,error,{},nullptr,{},&pieces,&cache));
        assert(shared.indices==reference.indices&&shared.batches.size()==reference.batches.size());
        for(size_t i=0;i<reference.batches.size();++i){const auto& a=reference.batches[i];const auto& b=shared.batches[i];
            assert(a.material==b.material&&a.start==b.start&&a.count==b.count);
            assert(a.pointOnly==b.pointOnly&&a.lampSourceId==b.lampSourceId);
            assert(b.pointOnly==scene.materials[b.material].pointOnly&&b.lampSourceId==scene.materials[b.material].lampSourceId);
        }
    }
    assert(cache.stats().hits>=2);
    std::puts("PASS shared mesh batches preserve point transmission and lamp identity on cold and warm builds");
}
static void testActual(const char* tile,const char* models){
    WorldScene scene;std::string error;
    assert(loadInstancedScenes({tile},models,{-10300,-573,-150},{-9724,3,490},scene,error));
    assert(!scene.triangles.empty());WorldMeshUploadPlan plan;
    auto start=std::chrono::steady_clock::now();assert(buildUploadPlan(scene,plan,error));
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    coherence(scene,plan);
    unsigned chunks=0;uint64_t sourceTextureBytes=0;
    for(auto&b:plan.batches)chunks+=b.terrain&&b.chunkX>=0;
    for(auto&m:scene.materials)sourceTextureBytes+=m.rgba.size();
    assert(chunks>0&&plan.unmaskableTerrainTriangles==0);
    std::printf("PASS actual tile plan: %u triangles, %zu batches, %u terrain batches, %zu material slots, %.3f MiB texture source -> %.3f MiB upload; %.3f seconds worker preparation\n",
        plan.triangleCount,plan.batches.size(),chunks,plan.materials.size(),double(sourceTextureBytes)/(1024*1024),double(plan.textureBytes)/(1024*1024),seconds);
}
// 0.3.216 (issue #66): spatial-major batch order. Same triangles per (material, chunk) as the material-major order, batches
// grouped by coarse cell, nonterrain stays chunk -1, terrain batch count unchanged, and a region far away does not move.
static void testSpatialOrder(){
    constexpr double origin=17066.666666666666,step=100.0/3.0;
    auto build=[&](const WorldScene& s,PlanOrder order){WorldMeshUploadPlan p;std::string error;assert(buildUploadPlan(s,p,error,{},nullptr,order));coherence(s,p);return p;};
    auto scene=[&](bool withExtra){
        WorldScene s;s.materials.resize(3);s.materials[0].terrain=true;
        for(int cy=100;cy<112;cy+=3)for(int cx=200;cx<212;cx+=3)for(uint32_t m:{0u,1u,2u}){
            if(m==0)chunkTriangle(s,cx,cy,0);
            else{float mx=float(origin-cy*step),my=float(origin-cx*step);triangle(s,{mx-4,my-4,1},{mx-8,my-4,2},{mx-4,my-9,3},m);}
        }
        if(withExtra){float mx=float(origin-100*step),my=float(origin-200*step);triangle(s,{mx-4,my-4,1},{mx-8,my-4,2},{mx-4,my-9,5},1);} // one more triangle in the first cell
        return s;
    };
    WorldScene a=scene(false),b=scene(true);
    auto old=build(a,{0,false}),spatial=build(a,{4,false});
    size_t oldTerrain=0,spatialTerrain=0;for(auto& x:old.batches)oldTerrain+=x.terrain;for(auto& x:spatial.batches)spatialTerrain+=x.terrain;
    assert(oldTerrain==spatialTerrain&&spatial.batches.size()>old.batches.size()&&spatial.triangleCount==old.triangleCount);
    // Batches are cell-major: the cell of each batch's triangles is constant and never decreases along the plan.
    auto cellOf=[&](const WorldScene& s,const WorldMeshUploadPlan& p,const Batch& x){
        std::set<uint32_t> cells;for(size_t j=x.start;j<x.start+size_t(x.count)*3;j+=3){
            double cxw=0,cyw=0;for(unsigned k=0;k<3;++k){auto v=s.vertices[p.indices[j+k]].position;cxw+=v.x/3;cyw+=v.y/3;}
            const int gx=int(std::floor((origin-cyw)/step)),gy=int(std::floor((origin-cxw)/step));cells.insert(uint32_t(gy/4)<<11|uint32_t(gx/4));}
        assert(cells.size()==1);return *cells.begin();};
    uint32_t last=0;for(auto& x:spatial.batches){const uint32_t c=cellOf(a,spatial,x);assert(c>=last);last=c;}
    // Insertion of a triangle changes only the batch of its own cell: the packed index run of every later cell is the same bytes.
    auto changed=build(b,{4,false});
    size_t equalBatches=0;
    for(auto& x:spatial.batches)for(auto& y:changed.batches)
        if(x.material==y.material&&x.chunkX==y.chunkX&&x.chunkY==y.chunkY&&x.count==y.count&&cellOf(a,spatial,x)==cellOf(b,changed,y)){
            bool same=true;for(uint32_t j=0;j<x.count*3&&same;++j){auto p=a.vertices[spatial.indices[x.start+j]].position,q=b.vertices[changed.indices[y.start+j]].position;same=p.x==q.x&&p.y==q.y&&p.z==q.z;}
            equalBatches+=same;}
    assert(equalBatches+1>=spatial.batches.size()-1);
    std::printf("PASS spatial batch order: %zu -> %zu batches (terrain %zu unchanged), cell-major, one extra triangle changes one batch\n",old.batches.size(),spatial.batches.size(),spatialTerrain);
}
int main(int argc,char**argv){testGrouping();testFailure();testTextureSource();testSharedBatchFlags();testShadowBounds();testSpatialOrder();if(argc>2)testActual(argv[1],argv[2]);std::puts("All world mesh upload plan tests passed");}
