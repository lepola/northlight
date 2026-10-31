#pragma once
#include "memory_footprint.h"
#include <chrono>
#include <cstring>
#include <mutex>

// Fixed-size diagnostic handoff. Producers inspect only their own immutable or
// thread-confined data, then publish numbers. The menu never walks scene owners,
// queries the D3D device, samples process memory or waits for a worker lock.
namespace NorthlightMemoryProfile {
inline uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Cpu {
    uint64_t at=0,buildId=0,bytes[NorthlightMemoryFootprint::KindCount]={};
    char stage[40]={};bool complete=false;
};
struct Admission {
    uint64_t at=0,available=0,largest=0,entryRequired=0,largestRequired=0,reserve=0,entryHeadroom=0,interiorHeadroom=0;
    uint64_t minimumAvailable=0,maximumKnownCapacity=0;char stage[40]={};
    bool valid=false,largestExact=false;
};
struct Gpu {
    uint64_t at=0,managed=0,defaults=0,system=0,geometry=0,probes=0,shadows=0,post=0,noise=0,dynamic=0;
    uint64_t retiredMaterials=0,materialReuseCpu=0;unsigned unknown=0;bool complete=false;
    uint64_t staticManagedLedger=0,replayDefaultLedger=0,liveTerrainDefault=0,captureCpu=0,captureIdentity=0;
};
struct Snapshot {Cpu builder,solver;Admission admission;Gpu gpu;};
class Store {
    std::mutex mutex_;Snapshot snapshot_;
public:
    void cpu(bool builder,const char* stage,uint64_t id,const NorthlightMemoryFootprint::Counter& memory){
        Cpu sample;sample.at=nowMs();sample.buildId=id;sample.complete=memory.complete();
        std::strncpy(sample.stage,stage,sizeof(sample.stage)-1);
        for(unsigned i=0;i<NorthlightMemoryFootprint::KindCount;++i)sample.bytes[i]=memory.bytes(NorthlightMemoryFootprint::Kind(i));
        std::lock_guard<std::mutex> lock(mutex_);(builder?snapshot_.builder:snapshot_.solver)=sample;
    }
    void admission(Admission sample){sample.at=nowMs();std::lock_guard<std::mutex> lock(mutex_);snapshot_.admission=sample;}
    void gpu(Gpu sample){sample.at=nowMs();std::lock_guard<std::mutex> lock(mutex_);snapshot_.gpu=sample;}
    void invalidateGpu(){std::lock_guard<std::mutex> lock(mutex_);snapshot_.gpu={};}
    bool tryRead(Snapshot& out){std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock)return false;out=snapshot_;return true;}
};
inline Store& store(){static Store value;return value;}
}
