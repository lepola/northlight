#include "world_mesh_pages.h"
#include "shadow_terrain.h"
#include "shadow_bounds.h"
#include "world_math.h"
#include "mesh_buffer_store.h"
#include "mesh_pool_trim.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_set>

using namespace NorthlightGI;
namespace P=NorthlightWorldMeshPages;
// 0.3.216 (issue #66): how much of the next build's pages an earlier build already holds, for sequential paging
// (anchorModulus 0) and content-defined paging, with the page counts and 64 KiB-rounded GPU capacity.
struct Sample {
    WorldScene local,far;NorthlightWorldMesh::WorldMeshUploadPlan input,oldInput;
    P::Plan sequential,anchored;double sequentialMs=0,anchoredMs=0;
};
static uint64_t roundedCapacity(const P::Plan& plan,uint64_t step=65536) {
    uint64_t bytes=0;for(const auto& p:plan.pages)for(uint64_t b:{uint64_t(p.vertices.size())*sizeof(WorldVertex),uint64_t(p.indices.size())*sizeof(uint16_t)})bytes+=(b+step-1)/step*step;return bytes;
}
static std::unique_ptr<Sample> sample(const std::string& root,Vec3 c,std::string& error) {
    int tx=int(std::floor((17066.6666667-c.y)/533.3333333)),ty=int(std::floor((17066.6666667-c.x)/533.3333333));
    std::vector<std::string> nearFiles,farFiles;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
        auto p=root+"/Azeroth/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        if(!std::filesystem::exists(p))continue;
        farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);
    }
    auto s=std::make_unique<Sample>();
    assert(loadInstancedScenes(nearFiles,root+"/models",c-Vec3(288,288,320),c+Vec3(288,288,320),s->local,error));
    const float radius=NorthlightShadowTerrain::Radius;
    assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),s->far,error,1));
    using Clock=std::chrono::steady_clock;
    auto ms=[](Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();};
    // OLD = material-major plan order + sequential pages; NEW = the production spatial order + content-defined pages.
    assert(NorthlightShadowTerrain::build(s->local,s->far,c,s->oldInput,error,{},NorthlightShadowTerrain::Radius,NorthlightWorldMesh::PlanOrder{0,false}));
    auto t=Clock::now();assert(P::build(s->oldInput,s->sequential,error,{},P::Options{0,0}));s->sequentialMs=ms(t);
    assert(NorthlightShadowTerrain::build(s->local,s->far,c,s->input,error));
    t=Clock::now();assert(P::build(s->input,s->anchored,error));s->anchoredMs=ms(t);
    return s;
}
// Fraction of `next` pages (and bytes) whose key equals a page of `prior`.
static void reuse(const P::Plan& prior,const P::Plan& next,size_t& pages,double& bytes) {
    std::unordered_set<NorthlightMeshPageKey::Key,NorthlightMeshPageKey::KeyHash> keys;
    for(const auto& p:prior.pages)keys.insert(p.key);
    size_t hit=0;double hitBytes=0,all=0;
    for(const auto& p:next.pages){const double b=double(p.key.vertexBytes)+p.key.indexBytes;all+=b;if(keys.count(p.key)){++hit;hitBytes+=b;}}
    pages=hit;bytes=all?hitBytes/all:0;
}
// A walk of `steps` commits (one plan per step): the store is driven like the renderer (staging, upload sizing, commit, trim
// decision). Gate: the store's peak pool stays below the old double-buffered pool (2 x the largest sequential mesh, 64 KiB
// rounding) and, on a diagonal walk, page reuse is far above the old sequential paging's. Reports the table of the variants.
static void walk(const std::string& root,const char* name,Vec3 c,Vec3 delta,unsigned steps,bool gateReuse) {
    namespace S=NorthlightMeshBufferStore;
    std::string error;S::Store store;NorthlightMeshPoolTrim::State trim;
    std::unique_ptr<Sample> prior;double peakPool=0,oldMax=0,newReuse=0,oldReuse=0,activeSum=0,oldMeshSum=0,peakCpu=0,peakCpuElided=0,elidedMiB=0,workerPeak=0,workerPeakElided=0;unsigned counted=0;size_t pagesSum=0,oldPagesSum=0,batchSum=0,oldBatchSum=0;
    for(unsigned k=0;k<steps;++k) {
        auto next=sample(root,c+delta*float(k),error);
        std::vector<S::Key> keys;for(const auto& p:next->anchored.pages)keys.push_back(p.key);
        // 0.3.216: the worker frees the data of pages the active buffers already hold (keys published at the last commit).
        const double cpuFull=double(next->anchored.cpuBytes());
        const auto resident=store.activeKeys();
        // The worker builds the plan with the resident keys: pages are freed inside build() as they finish.
        uint64_t peakPlain=0,peakElided=0;
        {P::Plan plain;P::Options o;o.peakLiveBytes=&peakPlain;assert(P::build(next->input,plain,error,{},o));}
        P::Options elidingOptions;elidingOptions.resident=&resident;elidingOptions.peakLiveBytes=&peakElided;
        assert(P::build(next->input,next->anchored,error,{},elidingOptions));
        P::ElisionStats elision;for(const auto& p:next->anchored.pages)if(p.elided){++elision.pages;elision.bytes+=p.key.vertexBytes+p.key.indexBytes;}
        for(uint32_t i=0;i<keys.size();++i)assert(next->anchored.pages[i].key==keys[i]); // elision never changes the paging
        assert(!P::validatePages(next->input,next->anchored)); // an elided plan still validates, with the full aggregate sizes
        std::vector<uint8_t> elidedFlags;for(const auto& p:next->anchored.pages)elidedFlags.push_back(p.elided);
        if(k){peakCpu=std::max(peakCpu,cpuFull);peakCpuElided=std::max(peakCpuElided,double(next->anchored.cpuBytes()));elidedMiB+=double(elision.bytes);workerPeak=std::max(workerPeak,double(peakPlain));workerPeakElided=std::max(workerPeakElided,double(peakElided));}
        assert(store.beginStaging(keys,elidedFlags)&&store.elidedMisses()==0); // every elided page hits: it came from the active table
        const auto& staging=store.staging();
        for(size_t u=0;u<staging.upload.size();++u) { // the Create steps of the upload cursor
            const uint32_t id=staging.uploadBuffer[u];const auto& key=keys[staging.upload[u]];assert(store.markWriting(id));
            if(store.entries()[id].vbCapacity<key.vertexBytes)assert(store.setCapacity(id,true,uint32_t(S::roundBuffer(key.vertexBytes))));
            if(store.entries()[id].ibCapacity<key.indexBytes)assert(store.setCapacity(id,false,uint32_t(S::roundBuffer(key.indexBytes))));
        }
        const auto stats=store.commit();
        for(uint32_t i=0;i<keys.size();++i){const auto& e=store.entries()[store.bufferOf(i)];assert(e.keyValid&&e.key==keys[i]&&e.vbCapacity>=keys[i].vertexBytes&&e.ibCapacity>=keys[i].indexBytes);}
        const uint64_t pool=store.poolBytes(),mesh=store.activeBytes();
        if(const uint64_t request=trim.onCommit(pool,mesh,k?stats.uploadedCapacity:0))store.trimIdle(request,[](uint32_t){});
        peakPool=std::max(peakPool,double(pool));const uint64_t oldMesh=roundedCapacity(next->sequential);oldMax=std::max(oldMax,double(oldMesh));
        if(prior) {
            size_t pages;double bytes;reuse(prior->sequential,next->sequential,pages,bytes);oldReuse+=bytes;
            newReuse+=double(stats.reusedBytes)/double(stats.reusedBytes+stats.uploadedBytes);++counted;
        }
        activeSum+=double(mesh);oldMeshSum+=double(oldMesh);pagesSum+=next->anchored.pages.size();oldPagesSum+=next->sequential.pages.size();
        batchSum+=next->anchored.batches.size();oldBatchSum+=next->sequential.batches.size();
        prior=std::move(next);
    }
    const double MiB=1048576.;
    std::printf("WALK %s %s: reuse old %.1f%% new %.1f%%; peak pool new %.1f MiB vs old 2x %.1f MiB; active mesh new %.1f old %.1f MiB; pages new %zu old %zu; batches new %zu old %zu (avg over %u commits); peak plan CPU without elision %.1f MiB, with %.1f MiB (elided %.1f MiB per commit); worker peak live page data without elision %.1f MiB, with %.1f MiB\n",name,gateReuse?"diagonal":"straight",
        oldReuse/counted*100,newReuse/counted*100,peakPool/MiB,2*oldMax/MiB,activeSum/steps/MiB,oldMeshSum/steps/MiB,pagesSum/steps,oldPagesSum/steps,batchSum/steps,oldBatchSum/steps,steps,peakCpu/MiB,peakCpuElided/MiB,elidedMiB/counted/MiB,workerPeak/MiB,workerPeakElided/MiB);
    assert(workerPeakElided<workerPeak*.85); // the worker itself holds clearly less while building
    assert(peakCpuElided<peakCpu*.85); // the staging plan holds clearly less CPU data once resident pages are elided
    assert(peakPool<2*oldMax);
    assert(newReuse/counted>oldReuse/counted+.15);
}
static void measure(const std::string& root,const char* name,Vec3 c) {
    std::string error;
    auto base=sample(root,c,error);
    std::printf("MEASURE %s: sequential pages %zu capacity %.1f MiB build %.0f ms; anchored pages %zu capacity %.1f MiB build+hash %.0f ms (key hashing only: ",name,
        base->sequential.pages.size(),double(roundedCapacity(base->sequential))/1048576,base->sequentialMs,base->anchored.pages.size(),double(roundedCapacity(base->anchored))/1048576,base->anchoredMs);
    {auto t=std::chrono::steady_clock::now();uint64_t sink=0;for(const auto& p:base->anchored.pages)sink+=NorthlightMeshPageKey::keyOf(p.vertices.data(),p.vertices.size()*sizeof(WorldVertex),p.indices.data(),p.indices.size()*sizeof(uint16_t)).lo&1;
     std::printf("%.1f ms for %.1f MiB%s)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count(),double(base->anchored.vertexBytes+base->anchored.indexBytes)/1048576,sink==UINT64_MAX?"!":"");}
    struct Move {const char* label;Vec3 delta;};
    for(const auto& move:{Move{"same center",{0,0,0}},Move{"+20 yd",{20,0,0}},Move{"+60 yd",{60,0,0}},Move{"+60 yd diagonal",{42,42,0}}}) {
        auto next=move.delta.x==0&&move.delta.y==0?sample(root,c,error):sample(root,c+move.delta,error);
        size_t oldPages,newPages;double oldBytes,newBytes;
        reuse(base->sequential,next->sequential,oldPages,oldBytes);reuse(base->anchored,next->anchored,newPages,newBytes);
        std::printf("MEASURE %s %s: old paging reuses %zu/%zu pages (%.1f%% bytes); new paging reuses %zu/%zu pages (%.1f%% bytes)\n",name,move.label,
            oldPages,next->sequential.pages.size(),oldBytes*100,newPages,next->anchored.pages.size(),newBytes*100);
        if(move.delta.x==0&&move.delta.y==0)assert(oldPages==next->sequential.pages.size()&&newPages==next->anchored.pages.size());
        else assert(newBytes>=oldBytes);
        if(move.delta.x>0&&move.delta.x<30)assert(newBytes>=.2&&newBytes>=oldBytes+.1); // a 20 yd step: clearly more than sequential paging
    }
}
int main(int argc,char** argv) {
    assert(argc==2);const std::string root=argv[1];std::string error;
    struct Fixture{const char* name;Vec3 center;};
    for(const auto& fixture:{Fixture{"Stormwind",{-8833,628,97}},Fixture{"Elwynn",{-9450,-200,65}}}) {
        const auto c=fixture.center;
        int tx=int(std::floor((17066.6666667-c.y)/533.3333333)),ty=int(std::floor((17066.6666667-c.x)/533.3333333));
        std::vector<std::string> nearFiles,farFiles;
        for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
            auto p=root+"/Azeroth/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
            if(!std::filesystem::exists(p))continue;
            farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);
        }
        WorldScene local,far;
        assert(loadInstancedScenes(nearFiles,root+"/models",c-Vec3(288,288,320),c+Vec3(288,288,320),local,error));
        const float radius=NorthlightShadowTerrain::Radius;
        assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),far,error,1));
        NorthlightWorldMesh::WorldMeshUploadPlan input;
        assert(NorthlightShadowTerrain::build(local,far,c,input,error));
        P::Plan pages;auto start=std::chrono::steady_clock::now();assert(P::build(input,pages,error));
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        assert(input.triangleCount>0);
        size_t originalBatch=0,originalTriangle=0,triangles=0;
        for(const auto& page:pages.pages) {
            assert(page.vertices.size()*sizeof(WorldVertex)<=P::PageVertexByteLimit);
            assert(page.indices.size()*sizeof(uint16_t)<=P::PageIndexByteLimit);
        }
        // Identity transform, normals and UVs are copied bit-for-bit. Therefore
        // identical VS/matrices produce identical vertex outputs, irrespective
        // of source index numbering or page. This does not execute a GPU shader.
        for(const auto& batch:pages.batches) {
            const auto& before=input.batches.at(originalBatch);const auto& page=pages.pages.at(batch.page);
            assert(batch.material==before.material&&batch.terrain==before.terrain&&batch.chunkX==before.chunkX&&batch.chunkY==before.chunkY);
            assert(originalTriangle+batch.count<=before.count);
            for(uint32_t t=0;t<batch.count;++t)for(unsigned k=0;k<3;++k) {
                const uint32_t prior=input.indices.at(before.start+(originalTriangle+t)*3+k);
                const uint32_t index=page.indices.at(batch.start+t*3+k);
                const auto& vertex=page.vertices.at(index);
                assert(index>=batch.minVertex&&index<batch.minVertex+batch.vertexCount);
                assert(prior<input.source->vertices.size()&&std::memcmp(&vertex,&input.source->vertices[prior],sizeof(WorldVertex))==0);
                const auto v=vertex.position;
                assert(v.x>=batch.boundsLow.x&&v.x<=batch.boundsHigh.x&&v.y>=batch.boundsLow.y&&v.y<=batch.boundsHigh.y&&v.z>=batch.boundsLow.z&&v.z<=batch.boundsHigh.z);
            }
            originalTriangle+=batch.count;triangles+=batch.count;
            if(originalTriangle==before.count){++originalBatch;originalTriangle=0;}
        }
        assert(triangles==input.triangleCount&&originalBatch==input.batches.size());
        // At low sun/moon directions, every rejected page subbatch must also
        // reject each constituent triangle's AABB. A tighter subdivision must
        // never remove a triangle intersecting the current shadow clip volume.
        unsigned rejected=0,directionalRejected=0;
        for(float degrees:{0.f,2.f,5.f,15.f,38.f})for(float azimuth:{0.f,1.7f,3.1f})for(float extent:{48.f,192.f}) {
            const float angle=degrees*.017453292519943295f;
            Vec3 direction(std::cos(angle)*std::cos(azimuth),std::cos(angle)*std::sin(azimuth),std::sin(angle));
            float matrix[16];NorthlightWorldMath::shadowMatrix(c,direction,extent,matrix);
            for(bool directional:{false,true})for(const auto& b:pages.batches) {
                auto reject=[&](Vec3 lo,Vec3 hi){return directional?NorthlightShadowBounds::directionalClipReject(lo,hi,matrix):NorthlightShadowBounds::clipReject(lo,hi,matrix);};
                if(!reject(b.boundsLow,b.boundsHigh))continue;
                if(directional)++directionalRejected;else ++rejected;
                const auto& page=pages.pages[b.page];
                for(uint32_t t=0;t<b.count;++t) {
                    Vec3 lo=page.vertices[page.indices[b.start+t*3]].position,hi=lo;
                    for(unsigned k=1;k<3;++k){const auto v=page.vertices[page.indices[b.start+t*3+k]].position;
                        lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);
                        hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);}
                    assert(reject(lo,hi));
                }
            }
        }
        const uint64_t originalCpu=input.cpuBytes();
        const size_t expectedOwnerCount=input.source->completePlacements.size();
        const auto localVertexCount=local.vertices.size();
        auto ownedPages=std::make_shared<P::Plan>(std::move(pages));
        assert(P::seal(input,ownedPages,error));assert(!P::validateSealed(input));
        assert(input.completePlacements.size()==expectedOwnerCount);
        const uint64_t sealedCpu=input.cpuBytes();
        assert(local.vertices.size()==localVertexCount);
        std::printf("OWNERSHIP %s original plan %.3f MiB -> pages + retained metadata %.3f MiB (delta %+.3f MiB)\n",fixture.name,double(originalCpu)/1048576.,double(sealedCpu)/1048576.,(double(sealedCpu)-double(originalCpu))/1048576.);
        const auto& sealedPages=*ownedPages;
        measure(root,fixture.name,c);
        walk(root,fixture.name,c,{14,14,0},8,true);
        walk(root,fixture.name,c,{20,0,0},8,false);
        std::printf("PASS %s full local + far terrain: vertices %u -> %llu; triangles %u exact; batches %zu -> %zu; pages %zu; worker %.3f ms; pages CPU %.3f MiB; conservative rejected subbatches clip=%u directional=%u at 0/2/5/15/38 degrees\n",fixture.name,input.vertexCount,(unsigned long long)(sealedPages.vertexBytes/sizeof(WorldVertex)),input.triangleCount,input.batches.size(),sealedPages.batches.size(),sealedPages.pages.size(),ms,double(sealedPages.cpuBytes())/1048576.,rejected,directionalRejected);
    }
}
