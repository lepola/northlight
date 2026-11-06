#include "world_mesh_pages.h"
#include "resource_memory_profile.h"
#include "geometry_memory.h"
#include "memory_profile_snapshot.h"
#include <thread>
#include <cassert>
#include <cstdio>
using namespace NorthlightGI;
using namespace NorthlightMemoryFootprint;
namespace R=NorthlightResourceMemory;

struct Texture {
    unsigned calls=0;bool fail=false;std::vector<D3DSURFACE_DESC> levels;
    UINT GetLevelCount()const{return UINT(levels.size());}
    HRESULT GetLevelDesc(UINT level,D3DSURFACE_DESC* out){++calls;*out=levels.at(level);return fail?E_FAIL:S_OK;}
};
struct Volume {
    D3DVOLUME_DESC desc={};UINT GetLevelCount()const{return 1;}
    HRESULT GetLevelDesc(UINT,D3DVOLUME_DESC* out){*out=desc;return S_OK;}
};
struct Surface {
    D3DSURFACE_DESC desc={};HRESULT GetDesc(D3DSURFACE_DESC* out){*out=desc;return S_OK;}
};
struct Buffer {
    D3DVERTEXBUFFER_DESC desc={};HRESULT GetDesc(D3DVERTEXBUFFER_DESC* out){*out=desc;return S_OK;}
};
static D3DSURFACE_DESC surfaceDesc(D3DPOOL pool,D3DFORMAT format,unsigned w,unsigned h){D3DSURFACE_DESC d={};d.Pool=pool;d.Format=format;d.Width=w;d.Height=h;return d;}
static void inventory(){
    Texture managed{0,false,{surfaceDesc(D3DPOOL_MANAGED,D3DFMT_A8R8G8B8,64,32),surfaceDesc(D3DPOOL_MANAGED,D3DFMT_A8R8G8B8,32,16)}};
    Texture target{0,false,{surfaceDesc(D3DPOOL_DEFAULT,D3DFMT_A16B16G16R16F,80,45)}};
    R::Inventory usage;usage.texture(static_cast<Texture*>(nullptr),R::Geometry);usage.texture(&managed,R::Geometry);usage.texture(&managed,R::Noise);usage.texture(&target,R::PostTargets);
    assert(managed.calls==2&&usage.pools[D3DPOOL_MANAGED]==(64*32+32*16)*4&&usage.pools[D3DPOOL_DEFAULT]==80*45*8);
    assert(usage.groups[R::Noise]==0&&usage.groups[R::Geometry]==usage.pools[D3DPOOL_MANAGED]);
    Volume volume;volume.desc.Pool=D3DPOOL_MANAGED;volume.desc.Format=D3DFMT_L8;volume.desc.Width=16;volume.desc.Height=8;volume.desc.Depth=4;
    usage.volume(&volume,R::Noise);assert(usage.groups[R::Noise]==16*8*4);
    Texture cube{0,false,{surfaceDesc(D3DPOOL_DEFAULT,D3DFMT_R32F,16,16)}};usage.cube(&cube,R::ShadowMaps);assert(usage.groups[R::ShadowMaps]==6*16*16*4);
    Surface depth{surfaceDesc(D3DPOOL_DEFAULT,D3DFMT_D24S8,8,8)};depth.desc.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;usage.surface(&depth,R::ShadowMaps);
    Surface readback{surfaceDesc(D3DPOOL_SYSTEMMEM,D3DFMT_R32F,32,16)};usage.surface(&readback,R::Geometry);assert(usage.pools[D3DPOOL_SYSTEMMEM]==32*16*4);
    Buffer buffer;buffer.desc.Pool=D3DPOOL_DEFAULT;buffer.desc.Size=512*1024;usage.buffer(&buffer,R::Dynamic,D3DVERTEXBUFFER_DESC{});
    assert(usage.complete()&&usage.groups[R::Dynamic]==512*1024&&usage.groups[R::ShadowMaps]==(6*16*16+8*8*4)*4);
    Texture bad{0,false,{surfaceDesc(D3DPOOL_DEFAULT,D3DFMT_UNKNOWN,8,8)}};usage.texture(&bad,R::Noise);assert(!usage.complete()&&usage.unknown==1);
    bad.fail=true;R::Inventory failure;failure.texture(&bad,R::Noise);assert(!failure.complete()&&!failure.pools[D3DPOOL_DEFAULT]);
    depth.desc.MultiSampleType=D3DMULTISAMPLE_NONMASKABLE;R::Inventory nonmaskable;nonmaskable.surface(&depth,R::ShadowMaps);assert(!nonmaskable.complete()&&!nonmaskable.groups[R::ShadowMaps]);
}
static WorldScene scene(){
    WorldScene s;s.materials.resize(1);auto& material=s.materials[0];material.width=8;material.height=8;material.rgba.assign(8*8*4,255);material.rgba[3]=0;material.alphaCutoff=.5f;
    s.vertices={{{0,0,0},{0,0,1},0,0},{{1,0,0},{0,0,1},1,0},{{0,1,0},{0,0,1},0,1}};
    s.triangles={{0,1,2,0}};return s;
}
int main(){
    inventory();
    {
        NorthlightMemoryProfile::Store store;NorthlightMemoryProfile::Snapshot snapshot;
        assert(store.tryRead(snapshot)&&!snapshot.builder.at&&!snapshot.solver.at&&!snapshot.gpu.at&&!snapshot.admission.at);
        std::thread writer([&]{for(uint64_t id=1;id<=2000;++id){Counter memory;memory.add(Vertices,id);memory.add(Triangles,id*2);store.cpu(true,"a-stage-name-that-is-longer-than-the-fixed-diagnostic-buffer",id,memory);}});
        for(unsigned i=0;i<2000;++i)if(store.tryRead(snapshot)&&snapshot.builder.at){assert(snapshot.builder.bytes[Vertices]==snapshot.builder.buildId&&snapshot.builder.bytes[Triangles]==snapshot.builder.buildId*2&&snapshot.builder.stage[39]==0);}
        writer.join();assert(store.tryRead(snapshot)&&snapshot.builder.buildId==2000);
        NorthlightMemoryProfile::Gpu gpu;gpu.managed=123;gpu.defaults=456;store.gpu(gpu);assert(store.tryRead(snapshot)&&snapshot.gpu.managed==123&&snapshot.gpu.defaults==456);
        store.invalidateGpu();assert(store.tryRead(snapshot)&&!snapshot.gpu.at);
    }
    Counter simple;std::vector<uint32_t> spare;spare.reserve(64);simple.vector(Upload,spare);simple.vector(Vertices,spare);
    assert(simple.bytes(Upload)==spare.capacity()*4&&!simple.bytes(Vertices));spare.clear();simple.vector(Upload,spare);assert(simple.total()==spare.capacity()*4);
    std::string tiny="a",large(1024,'x');Counter strings;strings.string(tiny);assert(!strings.total());strings.string(large);assert(strings.total()==large.capacity()+1);
    std::string error;auto bvh=std::make_shared<BVH>();assert(bvh->build(scene(),error));
    const auto before=bvh->trace({.2f,.2f,1},{0,0,-1});assert(before.hit);
    Counter graph;bvh->accountMemory(graph);const auto unique=graph.total();bvh->accountMemory(graph);bvh->scene().accountMemory(graph);
    assert(graph.total()==unique&&graph.bytes(Vertices)==bvh->scene().vertices.capacity()*sizeof(WorldVertex)&&graph.bytes(Triangles)==bvh->scene().triangles.capacity()*sizeof(WorldTriangle));
    const auto after=bvh->trace({.2f,.2f,1},{0,0,-1});assert(after.hit&&after.distance==before.distance&&after.triangle==before.triangle);
    NorthlightGeometryMemory::Generations<BVH> generations;assert(generations.track(bvh)&&generations.track(bvh));unsigned owners=0;generations.forEach([&](const BVH& item){++owners;item.accountMemory(graph);});assert(owners==1&&graph.total()==unique);
    NorthlightWorldMesh::WorldMeshUploadPlan plan;assert(NorthlightWorldMesh::buildUploadPlan(bvh->scene(),plan,error));
    auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();assert(NorthlightWorldMeshPages::build(plan,*pages,error));
    Counter prepared;bvh->accountMemory(prepared);plan.accountMemory(prepared);pages->accountMemory(prepared);const auto preseal=prepared.total();
    assert(prepared.bytes(Upload)>0&&prepared.bytes(Texels)>=8*8*4);
    assert(NorthlightWorldMeshPages::seal(plan,pages,error));
    auto payload=plan.payload.share();assert(payload&&payload->pages);std::weak_ptr<NorthlightWorldMesh::UploadPayload> weak=payload;
    Counter sealed;plan.accountMemory(sealed);const auto payloadUpload=sealed.bytes(Upload),payloadTexels=sealed.bytes(Texels),sealedTotal=sealed.total();
    payload->pages->accountMemory(sealed);plan.accountMemory(sealed);assert(sealed.total()==sealedTotal&&payloadUpload>0&&payloadTexels==8*8*4);
    auto copy=plan;copy.accountMemory(sealed);assert(sealed.bytes(Upload)==payloadUpload&&sealed.bytes(Texels)==payloadTexels); // copied handles share one upload allocation
    {
        // Actual commit moves texel vectors on the render thread. Diagnostic
        // readers use the immutable seal summary, not those mutable vectors.
        std::thread reader([&]{for(unsigned i=0;i<1000;++i){Counter usage;plan.accountMemory(usage);assert(usage.bytes(Upload)==payloadUpload&&usage.bytes(Texels)==payloadTexels);}});
        auto moved=std::move(payload->materials);reader.join();payload->materials=std::move(moved);
    }
    payload.reset();copy.payload.discard();assert(!weak.expired());plan.payload.discard();assert(weak.expired());
    Counter committed;plan.accountMemory(committed);assert(!committed.bytes(Upload)&&!committed.bytes(Texels)&&committed.total()<sealedTotal&&preseal>committed.total());
    assert(bvh->trace({.2f,.2f,1},{0,0,-1}).distance==before.distance); // upload release never removes CPU GI geometry
    NorthlightWorldMesh::PreparationCache cache;Counter empty;cache.accountMemory(empty);const auto emptyBytes=empty.total();cache.accountMemory(empty);assert(empty.total()==emptyBytes&&empty.complete());
    std::puts("PASS memory footprint: retained capacities, shared owners, upload release, GI unchanged, managed/default/system separation, mip/volume/cube/MSAA sizes, invalid descriptions; no GPU device");
}
