#include "regional_shadow_range.h"
#include "shadow_terrain.h"
#include "world_math.h"
#include "shadow_bounds.h"
#include "celestial_terrain.h"
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace NorthlightGI;
int main(int argc,char** argv){
    using namespace NorthlightRegionalShadow;
    Table table;std::string error;assert(load("shadow-range-profiles.ini",table,error));
    assert(table.at("Kalimdor",440)==4096&&table.at("Azeroth",440)==928&&table.at("Kalimdor",1637)==928);
    assert(table.at("Azeroth",4)==4096&&table.at("Kalimdor",215)==4096&&table.at("Kalimdor",17)==4096);
    assert(table.at("Kalimdor",4)==928&&table.at("Azeroth",215)==928&&table.at("Azeroth",17)==928);
    assert(table.at("Azeroth",12)==1856&&table.at("Kalimdor",12)==928&&table.at("Azeroth",1519)==928);
    for(const char* bad:{"[Kalimdor:440]\nterrain_radius=900", "[Kalimdor:440]\nterrain_radius=4097", "[Kalimdor:440]\nterrain_radius=nan", "[Unknown:440]\nterrain_radius=2048", "[Kalimdor:440]\n", "[Kalimdor:440]\nterrain_radius=2048\nterrain_radius=2048"}){
        std::istringstream in(bad);assert(!parse(in,table,error));assert(table.at("Kalimdor",440)==4096);
    }
    // Entire old square retained, including downward or distant-in-Z terrain.
    for(int x=-928;x<=928;x+=32)for(int y=-928;y<=928;y+=32)assert(selected({},Vec3(x,y,-9000),Vec3(x,y,9000)));
    assert(selected({},Vec3(2800,2800,0),Vec3(2801,2801,10)));
    assert(!selected({},Vec3(-2800,-2800,0),Vec3(-2799,-2799,10)));
    assert(!selected({},Vec3(2800,-2800,0),Vec3(2801,-2799,10)));
    // Every ray from the enlarged cached far receiver footprint, across every
    // orbital elevation, stays in the extra horizontal corridor. Include max
    // player/camera orbit and publication travel in both side directions.
    const float az=float(NorthlightCelestialOrbit::kAzimuthRadians);
    Vec3 right(-std::sin(az),std::cos(az),0);
    for(int elevation=0;elevation<=85;++elevation){
        auto b=NorthlightCelestialOrbit::body(elevation);Vec3 direction(b.direction[0],b.direction[1],b.direction[2]);
        for(float side:{-416.f,416.f})for(float distance:{0.f,640.f,2048.f,4096.f}){
            Vec3 p=right*side+direction*distance;assert(selected({},p-Vec3(1,1,1),p+Vec3(1,1,1)));
        }
    }
    if(argc<2){puts("regional shadow policy PASS");return 0;}
    const std::string root=argv[1],map=argc>=6?argv[2]:"Kalimdor";
    Vec3 center=argc>=6?Vec3(std::stof(argv[3]),std::stof(argv[4]),std::stof(argv[5])):Vec3(-9000,-3500,40);
    for(float reach:{928.f,4096.f}){
        const bool extended=reach>928;int n=extended?int(std::ceil(reach/NorthlightRegionalFog::TileSize)):2;
        int tx=int(std::floor((NorthlightRegionalFog::WorldZero-center.y)/NorthlightRegionalFog::TileSize)),ty=int(std::floor((NorthlightRegionalFog::WorldZero-center.x)/NorthlightRegionalFog::TileSize));
        std::vector<std::string> files;
        std::function<bool(Vec3,Vec3)> filter,chunkFilter;
        if(extended)chunkFilter=[&](Vec3 lo,Vec3 hi){return selectedChunk(center,lo,hi);};
        if(extended)filter=[&](Vec3 lo,Vec3 hi){return selected(center,lo,hi);};
        for(int y=ty-n;y<=ty+n;++y)for(int x=tx-n;x<=tx+n;++x){
            const double z=NorthlightRegionalFog::WorldZero,s=NorthlightRegionalFog::TileSize;
            if(extended&&!filter(Vec3(float(z-(y+1)*s),float(z-(x+1)*s),-100000),Vec3(float(z-y*s),float(z-x*s),100000)))continue;
            std::string path=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
            if(auto f=std::fopen(path.c_str(),"rb")){std::fclose(f);files.push_back(path);}
        }
        WorldScene scene;auto started=std::chrono::steady_clock::now();
        if(!loadInstancedScenes(files,root+"/models",center-Vec3(reach,reach,reach),center+Vec3(reach,reach,reach),scene,error,1,{},filter,chunkFilter)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        assert(!scene.triangles.empty());for(auto& m:scene.materials)assert(m.terrain);
        NorthlightWorldMesh::WorldMeshUploadPlan plan;WorldScene empty;
        assert(NorthlightShadowTerrain::build(empty,scene,center,plan,error,{},reach));
        BVH bvh;assert(bvh.build(std::move(scene),error));unsigned distantHits=0,drawable=0;
        for(float x:{center.x-300,center.x,center.x+300})for(float y:{center.y-300,center.y,center.y+300}){
            auto ground=bvh.trace(Vec3(x,y,1000),Vec3(0,0,-1),0,2000);if(!ground.hit)continue;
            Vec3 origin=ground.position+Vec3(0,0,2);
            for(float e:{.2f,1.f,2.f,3.f,5.f,8.f,12.f}){
                auto body=NorthlightCelestialOrbit::body(e);Vec3 dir(body.direction[0],body.direction[1],body.direction[2]);
                auto hit=bvh.trace(origin,dir,.1f,6000);if(!hit.hit||hit.distance<=1313)continue;++distantHits;
                float matrix[16];NorthlightWorldMath::shadowMatrix(origin,dir,240,matrix);
                const auto& t=bvh.scene().triangles[hit.triangle];const auto& s=bvh.scene();auto a=s.vertices[t.v0].position,b=s.vertices[t.v1].position,c=s.vertices[t.v2].position;
                Vec3 lo(std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z}));
                Vec3 hi(std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z}));
                assert(!NorthlightShadowBounds::directionalClipReject(lo,hi,matrix));
                NorthlightCelestialDisc::Disc disc;const float tint[]={1,1,1},camera[]={origin.x,origin.y,origin.z};
                assert(NorthlightCelestialDisc::prepare(body.direction,tint,1,.02f,disc));
                NorthlightCelestialTerrain::matrix(disc,camera,2.5f,1024,matrix);
                assert(!NorthlightCelestialTerrain::reject(lo,hi,matrix));
                const float p[]={hit.position.x,hit.position.y,hit.position.z};double clip[4]={};
                for(unsigned col=0;col<4;++col){clip[col]=matrix[12+col];for(unsigned row=0;row<3;++row)clip[col]+=double(p[row])*matrix[row*4+col];}
                assert(clip[3]>0&&clip[2]>0&&clip[2]<clip[3]);
                assert(std::fabs(clip[0]/clip[3]+1./1024)<.001&&std::fabs(clip[1]/clip[3]-1./1024)<.001);
                assert(plan.fixedTerrainChunks.count(NorthlightShadowTerrain::chunk(a,b,c)));++drawable;
            }
        }
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        printf("radius=%.0f files=%zu triangles=%zu uploadMiB=%.2f fixedChunks=%zu distantRays=%u drawableRays=%u offlineMs=%.1f\n",reach,files.size(),bvh.scene().triangles.size(),double(plan.vertexBytes+plan.indexBytes)/1048576,plan.fixedTerrainChunks.size(),distantHits,drawable,ms);
        if(extended&&argc<6)assert(distantHits>0);
    }
}
