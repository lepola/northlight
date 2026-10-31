#include "scene_source_cache.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <thread>
using namespace NorthlightGI;
static void u32(FILE* f,uint32_t n){assert(std::fwrite(&n,4,1,f)==1);}
static void scalar(FILE* f,float n){assert(std::fwrite(&n,4,1,f)==1);}
static void write(const std::filesystem::path& path,float x){
    FILE* f=std::fopen(path.string().c_str(),"wb");assert(f);
    assert(std::fwrite("FGS2",4,1,f)==1);for(uint32_t n:{2u,3u,1u,1u})u32(f,n);
    for(auto p:{Vec3{x,0,0},Vec3{x+1,0,0},Vec3{x,1,0}}){
        for(float n:{p.x,p.y,p.z,0.f,0.f,1.f,0.f,0.f})scalar(f,n);
    }
    for(uint32_t n:{0u,1u,2u,0u})u32(f,n);
    for(float n:{.4f,.5f,.6f})scalar(f,n);u32(f,1);u32(f,1);scalar(f,.5f);u32(f,4);u32(f,0xffffffffu);
    std::fclose(f);
}
int main(int argc,char** argv){
    assert(argc==2);namespace fs=std::filesystem;fs::path root=argv[1];fs::create_directories(root);
    auto a=root/"a.fgs",b=root/"b.fgs";write(a,0);write(b,5);
    std::string error;SceneSourceCache cache;auto first=cache.get(a.string(),error);assert(first&&error.empty());
    auto again=cache.get(a.string(),error);assert(again==first&&cache.stats().hits==1&&cache.stats().reads==1);
    const auto charge=cache.liveBytes();assert(charge>first->retainedBytes());
    cache.setLimit(charge);auto second=cache.get(b.string(),error);
    assert(second&&second!=first&&second->vertices[0].position.x==5);
    assert(cache.liveBytes()>0&&cache.liveBytes()<=charge&&cache.entries()==0&&cache.stats().uncached==1); // pinned first prevents replacement
    cache.reset();assert(cache.liveBytes()>0&&cache.liveBytes()<=charge);first.reset();again.reset();assert(!cache.liveBytes());
    second.reset();auto next=cache.get(b.string(),error);assert(next&&cache.liveBytes()<=charge);
    cache.setLimit(0);assert(cache.liveBytes()>0&&!cache.entries());next.reset();assert(!cache.liveBytes());
    auto uncached=cache.get(a.string(),error);assert(uncached&&!cache.liveBytes()&&!cache.entries()&&cache.uncachedLeaseBytes()>0&&cache.decodedLiveBytes()>0);
    cache.setLimit(8u*1024u*1024u);auto held=cache.get(a.string(),error);assert(held);
    auto oldStamp=fs::last_write_time(a);write(a,9);fs::last_write_time(a,oldStamp+std::chrono::seconds(2));
    auto replacement=cache.get(a.string(),error);assert(replacement&&replacement!=held&&replacement->vertices[0].position.x==9);
    assert(held->vertices[0].position.x==0&&cache.stats().changed==1);
    write(a,0);fs::last_write_time(a,oldStamp);
    auto restored=cache.get(a.string(),error);assert(restored&&restored->sourceIdentity!=held->sourceIdentity&&restored->sourceIdentity!=replacement->sourceIdentity);
    {FILE* f=std::fopen(a.string().c_str(),"wb");assert(f);std::fputs("bad",f);std::fclose(f);}
    assert(!cache.get(a.string(),error)&&!error.empty());assert(replacement->vertices[0].position.x==9);
    assert(!cache.get(b.string(),error,[](uint64_t){return false;})&&!error.empty());
    cache.reset();held.reset();replacement.reset();restored.reset();assert(!cache.liveBytes());
    auto provider=cache.provider();assert(provider(b.string(),error,{}));assert(cache.stats().reads>=6);
    auto canonical=cache.get(b.string(),error);const auto token=canonical->sourceIdentity;assert(token);canonical.reset();
    cache.setLimit(512);assert(!cache.entries()); // keep only the small identity record
    auto reconstructed=cache.get(b.string(),error);assert(reconstructed&&reconstructed->sourceIdentity==token);
    assert(cache.stats().identityHits>0&&cache.liveBytes()<=512);
    cache.reset();auto afterReset=cache.get(b.string(),error);assert(afterReset&&afterReset->sourceIdentity!=token);
    cache.setLimit(8u*1024u*1024u);
    auto bStamp=fs::last_write_time(b);const auto priorToken=afterReset->sourceIdentity;
    write(b,6);fs::last_write_time(b,bStamp+std::chrono::seconds(2));assert(!cache.findIdentity(b.string()));
    write(b,5);fs::last_write_time(b,bStamp);auto revisionReturn=cache.get(b.string(),error);
    assert(revisionReturn&&revisionReturn->sourceIdentity!=priorToken);const auto returnToken=revisionReturn->sourceIdentity;
    {FILE* f=std::fopen(b.string().c_str(),"wb");assert(f);std::fputs("bad",f);std::fclose(f);}
    assert(!cache.get(b.string(),error));write(b,5);fs::last_write_time(b,bStamp);
    auto failedReturn=cache.get(b.string(),error);assert(failedReturn&&failedReturn->sourceIdentity!=returnToken);
    {
        SceneSourceCache racing(512);auto beforeRace=racing.get(b.string(),error);assert(beforeRace&&beforeRace->sourceIdentity);
        bool changed=false;auto failed=racing.get(b.string(),error,[&](uint64_t){if(!changed){changed=true;fs::last_write_time(b,bStamp+std::chrono::seconds(4));}return true;});
        assert(changed&&!failed&&!error.empty());fs::last_write_time(b,bStamp);
        auto afterRace=racing.get(b.string(),error);assert(afterRace&&afterRace->sourceIdentity!=beforeRace->sourceIdentity);
    }
    std::thread workers[3];for(auto& worker:workers){worker=std::thread([&]{
        for(unsigned i=0;i<200;++i){std::string e;auto value=cache.get(b.string(),e);assert(value&&value->vertices[0].position.x==5&&e.empty());
            auto found=cache.find(b.string(),e);if(found)assert(found->vertices[0].position.x==5);
            if(i%31==0)cache.reset();assert(cache.liveBytes()<=8u*1024u*1024u);}
    });}
    for(auto& worker:workers)worker.join();
    cache.reset();afterReset.reset();reconstructed.reset();uncached.reset();revisionReturn.reset();failedReturn.reset();assert(!cache.liveBytes()&&!cache.decodedLiveBytes());
    std::puts("PASS source pool: shared reads, LRU, pinned leases/reset charge, unchanged caps, replacement/corruption, admission, provider");
}
