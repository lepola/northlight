#include "world_gi.h"
#include "shadow_terrain.h"
#include "world_mesh_pages.h"
#include "mesh_page_key.h"
#include "world_probe_progress.h"
#include "regional_shadow_range.h"
#ifndef PIPELINE_RADIUS
#define PIPELINE_RADIUS 928
#endif
#if SHARED_SCENE_PIPELINE
#include "scene_source_cache.h"
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
using namespace NorthlightGI;
using Hash=NorthlightMeshPageKey::Hasher;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static void vec(Hash& h,Vec3 v){h.bytes(&v,sizeof v);}
static void material(Hash& h,const WorldMaterial& m){
    vec(h,m.albedo);h.word(m.width);h.word(m.height);h.bytes(&m.alphaCutoff,sizeof m.alphaCutoff);
    h.word(m.addressU);h.word(m.addressV);h.word(m.terrain);h.word(m.wmo);h.word(m.rgba.size());h.bytes(m.rgba.data(),m.rgba.size());
}
static void scene(Hash& h,const WorldScene& s){
    h.word(s.vertices.size());h.word(s.triangles.size());h.word(s.materials.size());
    h.bytes(s.vertices.data(),s.vertices.size()*sizeof(WorldVertex));h.bytes(s.triangles.data(),s.triangles.size()*sizeof(WorldTriangle));
    for(const auto& m:s.materials)material(h,m);
    for(const auto& p:s.completePlacements){h.word(p.uid);h.word(p.category);h.bytes(p.modelKey.data(),p.modelKey.size());h.bytes(p.matrix,sizeof p.matrix);vec(h,p.translation);vec(h,p.low);vec(h,p.high);}
}
template<class Batch> static void batch(Hash& h,const Batch& b){
    h.word(b.material);h.word(b.start);h.word(b.count);h.word(b.terrain);
    h.word(uint32_t(b.chunkX));h.word(uint32_t(b.chunkY));vec(h,b.boundsLow);vec(h,b.boundsHigh);
}
static void printHash(const char* name,const Hash& h){auto k=h.finish(0,0);std::printf(" %s=%016llx%016llx",name,(unsigned long long)k.lo,(unsigned long long)k.hi);}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const std::string root=argv[1],map=argv[2];const bool bake=argc==4;
    assert(configureProbeLayout(probeLayoutFor(20)));const auto lighting=prepareLighting(Lighting{});assert(lighting.valid());
#if SHARED_SCENE_PIPELINE
    LocalSceneCache cache({16u*1024u*1024u});SceneSourceCache sources(8u*1024u*1024u);const SceneSourceProvider provider=sources.provider();
    const SceneSourceRevision revision=[&](const std::string& path){return sources.findIdentity(path);};
    NorthlightWorldMesh::PreparationCache preparation;preparation.setLimit(8u*1024u*1024u);
#else
    LocalSceneCache cache;
#endif
    NorthlightMeshPageKey::KeySet resident;SceneSizeHint hint;
    const Vec3 origin=map=="Northrend"?Vec3(5248,10,508):Vec3(-8833,628,97);
    const Vec3 moves[]={{0,0,0},{0,0,0},{32,0,0},{0,32,0},{-32,0,0},{0,-32,0},{0,0,0}};
    const float radius=float(PIPELINE_RADIUS);const bool extended=radius>NorthlightShadowTerrain::Radius;const int tileReach=extended?int(std::ceil(radius/(1600./3))):2;
    for(unsigned step=0;step<7;++step){
        const Vec3 c=origin+moves[step];const int tx=int(std::floor((17066.666666666666-c.y)/533.3333333333333)),ty=int(std::floor((17066.666666666666-c.x)/533.3333333333333));
        std::vector<std::string> nearFiles,farFiles;
        for(int y=ty-tileReach;y<=ty+tileReach;++y)for(int x=tx-tileReach;x<=tx+tileReach;++x){
            auto path=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(!std::filesystem::exists(path))continue;
            if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(path);
            constexpr double zero=17066.666666666666,size=1600./3;
            if(!extended||NorthlightRegionalShadow::selected(c,{float(zero-(y+1)*size),float(zero-(x+1)*size),-100000},{float(zero-y*size),float(zero-x*size),100000}))farFiles.push_back(path);
        }
        assert(!nearFiles.empty()&&!farFiles.empty());std::string error;BVH local;WorldScene terrain;
        auto all=Clock::now(),t=all;
#if SHARED_SCENE_PIPELINE
        assert(cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error,{},true,provider,revision));
#else
        assert(cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error));
#endif
        double localMs=ms(t);t=Clock::now();
        std::function<bool(Vec3,Vec3)> terrainFilter,terrainChunkFilter;
        if(extended){terrainFilter=[c](Vec3 lo,Vec3 hi){return NorthlightRegionalShadow::selected(c,lo,hi);};terrainChunkFilter=[c](Vec3 lo,Vec3 hi){return NorthlightRegionalShadow::selectedChunk(c,lo,hi);};}
#if SHARED_SCENE_PIPELINE
        assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),terrain,error,1,{},terrainFilter,terrainChunkFilter,&hint,provider
#if CURRENT_TERRAIN_CHUNKS
            ,true
#endif
        ));
#else
        assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),terrain,error,1,{},terrainFilter,terrainChunkFilter,&hint));
#endif
        double terrainMs=ms(t);t=Clock::now();NorthlightWorldMesh::WorldMeshUploadPlan plan;
#if SHARED_SCENE_PIPELINE
        assert(NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,{},radius,{},true,&local,&preparation));
#else
        assert(NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,{},radius,{},true));
#endif
        double planMs=ms(t);t=Clock::now();NorthlightWorldMeshPages::Plan pages;NorthlightWorldMeshPages::Options options;options.resident=&resident;
#if SHARED_SCENE_PIPELINE
        options.preparation=&preparation;
#endif
        assert(NorthlightWorldMeshPages::build(plan,pages,error,{},options));double pagesMs=ms(t),pipelineMs=ms(all);
        Hash localHash,planHash,pageHash,rayHash,bakeHash;scene(localHash,local.scene());
        planHash.word(plan.indices.size());planHash.bytes(plan.indices.data(),plan.indices.size()*4);
        for(const auto& b:plan.batches)batch(planHash,b);
        for(const auto& m:plan.materials){planHash.word(m.width);planHash.word(m.height);planHash.word(m.bgra.size());planHash.bytes(m.bgra.data(),m.bgra.size());planHash.bytes(&m.alphaCutoff,4);planHash.word(m.terrain);}
        for(const auto& chunk:plan.fixedTerrainChunks){planHash.word(uint32_t(chunk.first));planHash.word(uint32_t(chunk.second));}
        planHash.word(bool(plan.localShadowRecords));if(plan.localShadowRecords)for(const auto& record:*plan.localShadowRecords){
            vec(planHash,record.low);vec(planHash,record.high);planHash.word(record.content.words[0]);planHash.word(record.content.words[1]);planHash.word(record.content.triangles);
            planHash.word(uint32_t(record.chunkX));planHash.word(uint32_t(record.chunkY));planHash.word(record.terrain);
        }
        resident.clear();for(const auto& p:pages.pages){resident.insert(p.key);pageHash.word(p.key.lo);pageHash.word(p.key.hi);pageHash.word(p.key.vertexBytes);pageHash.word(p.key.indexBytes);}
        for(const auto& b:pages.batches){pageHash.word(b.page);pageHash.word(b.minVertex);pageHash.word(b.vertexCount);batch(pageHash,b);}
        t=Clock::now();uint32_t rng=0x7123abcd;auto random=[&]{rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.f;};
        for(unsigned i=0;i<8192;++i){Vec3 o=c+Vec3((random()-.5f)*700,(random()-.5f)*700,(random()-.5f)*300),d(random()-.5f,random()-.5f,random()-.5f);
            const auto hit=local.trace(o,d,.001f,512.f);rayHash.word(hit.hit);rayHash.word(hit.backFace);rayHash.word(hit.triangle);rayHash.bytes(&hit.distance,4);
            vec(rayHash,hit.position);vec(rayHash,hit.normal);vec(rayHash,hit.geometricNormal);vec(rayHash,hit.albedo);rayHash.word(local.occluded(o,d,512.f,.001f));
        }
        const double traceMs=ms(t);double bakeMs=0;
        if(bake){t=Clock::now();const auto window=probeWindowOrigin(c);
            for(unsigned i=0;i<probeLayout().count();++i){const auto position=probeWindowPosition(window,i);ProbeGridKey key;assert(probeGridKey(position,key));ProbeResponse response;
                const auto p=solveProbeResponse(local,position,lighting,64,probeSeed(key),response);
                vec(bakeHash,p.position);for(const auto& sh:p.sh)vec(bakeHash,sh);for(const auto& m:p.moments){bakeHash.bytes(&m.mean,4);bakeHash.bytes(&m.meanSquare,4);}
                bakeHash.word(p.samples);bakeHash.word(p.backFaceSamples);bakeHash.word(p.valid);bakeHash.bytes(&p.maxDistance,4);
                for(const auto& source:response.sources)for(const auto& coefficient:source)vec(bakeHash,coefficient);
            }bakeMs=ms(t);
        }
        std::printf("PIPELINE step=%u localMs=%.4f terrainMs=%.4f planMs=%.4f pagesMs=%.4f totalMs=%.4f traceMs=%.4f bakeMs=%.4f prepareAndBakeMs=%.4f localTriangles=%zu planTriangles=%u pages=%zu batches=%zu retainedBytes=%zu",step,localMs,terrainMs,planMs,pagesMs,pipelineMs,traceMs,bakeMs,pipelineMs+bakeMs,local.triangleCount(),plan.triangleCount,pages.pages.size(),pages.batches.size(),local.retainedBytes());
        printHash("scene",localHash);printHash("plan",planHash);printHash("pagesHash",pageHash);printHash("rays",rayHash);printHash("bakeHash",bakeHash);
#if SHARED_SCENE_PIPELINE
        const auto& s=cache.stats();const auto& p=preparation.stats();
        std::printf(" blasBuilt=%llu blasReused=%llu tlasPieces=%llu fallback=%llu sourceHits=%llu sourceReads=%llu sourceLiveBytes=%llu preparationHits=%llu reusedTriangles=%llu preparationBytes=%llu",(unsigned long long)s.blasBuilds,(unsigned long long)s.blasReuses,(unsigned long long)s.tlasPieces,(unsigned long long)s.legacyFallbacks,(unsigned long long)sources.stats().hits,(unsigned long long)sources.stats().reads,(unsigned long long)sources.liveBytes(),(unsigned long long)p.hits,(unsigned long long)p.reusedTriangles,(unsigned long long)p.bytes);
#endif
        std::puts("");
    }
}
