#include "scene_source_cache.h"
#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
using namespace NorthlightGI;
namespace NorthlightDiagnostics {static bool enabled(){return false;}}
template<class... T> static void logf(const char*,T...){}
struct FakeSources {
    unsigned trims=0,resets=0;
    void trimPayloads(){++trims;}void reset(){++resets;}
    uint64_t liveBytes()const{return resets?0:trims?1024:6u*1024u*1024u;}
};
struct FakeLocal {unsigned resets=0;void reset(){++resets;}};
struct FakePreparation {struct Stats{uint64_t bytes;};unsigned resets=0;void reset(){++resets;}Stats stats()const{return {resets?0u:8u*1024u*1024u};}};
static void policy(){
    using namespace NorthlightGeometryMemory;const auto plainEntry=buildBudget(64*MiB);assert(plainEntry.available==512*MiB&&plainEntry.largest==96*MiB);
    struct Request {uint64_t id=17;}r;
    for(unsigned raised=0;raised<2;++raised)for(unsigned outcome=0;outcome<7;++outcome){
        auto entryBudget=plainEntry;if(raised){entryBudget.available+=64*MiB;entryBudget.largest+=16*MiB;}
        FakeSources sceneSources;FakeLocal localGeometry;FakePreparation meshPreparation;
        Sample memory={entryBudget.available-(outcome?MiB:0),entryBudget.largest,true};
        if(outcome==5)memory={entryBudget.available,entryBudget.largest,false};
        if(outcome==6)memory={entryBudget.available,entryBudget.largest-1,true};
        if(raised&&outcome>0&&outcome<5)assert(admits(memory,plainEntry));
        unsigned samples=0;
        auto workerMemory=[&](Budget budget){assert(budget.available==entryBudget.available&&budget.largest==entryBudget.largest);++samples;
            if(outcome<=3&&samples>=outcome)return Sample{budget.available,budget.largest,true};
            return memory;};
        auto resetPreparation=[&]{meshPreparation.reset();sceneSources.reset();localGeometry.reset();};
#include "scene_entry_reclaim.inl"
        if(!outcome){assert(!samples&&!sceneSources.trims&&!sceneSources.resets&&!localGeometry.resets&&!meshPreparation.resets);}
        else if(outcome==1){assert(samples==1&&sceneSources.trims==1&&!localGeometry.resets&&!meshPreparation.resets&&!sceneSources.resets);}
        else if(outcome==2){assert(samples==2&&sceneSources.trims==1&&localGeometry.resets==1&&!meshPreparation.resets&&!sceneSources.resets);}
        else {assert(samples==3&&sceneSources.trims==1&&localGeometry.resets==2&&meshPreparation.resets==1&&sceneSources.resets==1);}
        assert(admits(memory,entryBudget)==(outcome<=3));
    }
}
static void write(const std::filesystem::path& path,float x){
    FILE* f=std::fopen(path.string().c_str(),"wb");assert(f);assert(std::fwrite("FGS2",4,1,f)==1);
    const uint32_t header[]={2,3,1,1};assert(std::fwrite(header,sizeof header,1,f)==1);
    const float vertices[]={x,0,0,0,0,1,0,0, x+1,0,0,0,0,1,1,0, x,1,0,0,0,1,0,1};assert(std::fwrite(vertices,sizeof vertices,1,f)==1);
    const uint32_t triangle[]={0,1,2,0};assert(std::fwrite(triangle,sizeof triangle,1,f)==1);
    const float color[]={.4f,.5f,.6f};assert(std::fwrite(color,sizeof color,1,f)==1);
    const uint32_t texture[]={1,1,0x3f000000,4,0xffffffff};assert(std::fwrite(texture,sizeof texture,1,f)==1);std::fclose(f);
}
int main(int argc,char** argv){
    assert(argc==2);policy();namespace fs=std::filesystem;const fs::path root=argv[1];fs::create_directories(root);const auto path=root/"source.fgs";write(path,2);
    SceneSourceCache cache;std::string error;auto held=cache.get(path.string(),error);assert(held&&held->sourceIdentity);const auto token=held->sourceIdentity,bytes=cache.liveBytes();
    cache.trimPayloads();assert(cache.entries()==0&&cache.liveBytes()==bytes&&held->vertices[0].position.x==2); // a published lease cannot be freed speculatively
    assert(cache.findIdentity(path.string())==token);held.reset();const auto metadata=cache.liveBytes();assert(metadata&&metadata<bytes&&!cache.decodedLiveBytes());
    auto reloaded=cache.get(path.string(),error);assert(reloaded&&reloaded->sourceIdentity==token&&cache.stats().reads==2);
    reloaded.reset();cache.trimPayloads();assert(cache.liveBytes()==metadata&&cache.findIdentity(path.string())==token);
    const auto time=fs::last_write_time(path);write(path,9);fs::last_write_time(path,time+std::chrono::seconds(2));assert(!cache.findIdentity(path.string()));
    auto changed=cache.get(path.string(),error);assert(changed&&changed->sourceIdentity!=token&&changed->vertices[0].position.x==9);const auto changedToken=changed->sourceIdentity;
    changed.reset();cache.trimPayloads();write(path,2);fs::last_write_time(path,time);auto returned=cache.get(path.string(),error);assert(returned&&returned->sourceIdentity!=token&&returned->sourceIdentity!=changedToken);
    returned.reset();cache.trimPayloads();assert(!cache.get(path.string(),error,[](uint64_t){return false;})&&!error.empty());
    cache.reset();assert(!cache.liveBytes()&&!cache.decodedLiveBytes());auto reset=cache.get(path.string(),error);assert(reset&&reset->sourceIdentity!=token);
    cache.trimPayloads();cache.setLimit(0);assert(!cache.findIdentity(path.string())&&cache.entries()==0);reset.reset();assert(!cache.liveBytes()&&!cache.decodedLiveBytes());
    std::puts("PASS scene entry reclaim: plain and raised floors, raw/local/full actions, invalid/largest refusal, pinned lease, stable revision, ABA, failure, reset/lowered cap");
}
