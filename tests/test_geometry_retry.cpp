#include "world_streaming.h"
#include "geometry_memory.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <vector>

using V=NorthlightGI::Vec3;
using DWORD=uint32_t;
static uint64_t tick=1000;
static DWORD GetTickCount(){return DWORD(tick);}
struct Request {uint64_t id=1;std::string map="Northrend";V camera,geometryCenter;};
struct Event {uint64_t at;Request request;bool stopping=false,exiting=false;};
// Deterministic condition-variable notifications: execute the real worker's
// wait predicate for every event and timeout without a game or wall-clock sleeps.
struct Wake {
    std::vector<Event> events;size_t next=0;Request& request;bool& stopping;bool& builderExit;
    template<class Predicate> bool wait_for(std::unique_lock<std::mutex>& lock,std::chrono::milliseconds delay,Predicate predicate){
        assert(lock.owns_lock());if(predicate())return true;const auto deadline=tick+delay.count();
        while(next<events.size()&&events[next].at<=deadline){
            const auto& event=events[next++];assert(event.at>=tick);tick=event.at;request=event.request;stopping=event.stopping;builderExit=event.exiting;
            if(predicate())return true;
        }
        tick=deadline;return false;
    }
};
struct Stand {
    std::mutex mutex;bool stopping=false,builderExit=false,retry=false;Request request;
    Wake buildWake{{},0,request,stopping,builderExit};DWORD memoryReportAt=0;unsigned abandoned=0,reports=0;
    void logGeometryMemory(const char*,NorthlightGeometryMemory::Sample,unsigned,bool,const NorthlightGeometryMemory::Budget*){++reports;}
    struct Generations {unsigned live()const{return 1;}} generations;bool workerMemoryExact=true;
    void abandonGeometry(){++abandoned;}
    void once(unsigned delay=1000){
        const auto r=request;
#include "geometry_retry.inl"
        const auto budget=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);
        NorthlightGeometryMemory::Sample memory;memory.valid=true;memory.available=budget.available-45*NorthlightGeometryMemory::MiB;memory.largest=580*NorthlightGeometryMemory::MiB;
        assert(!NorthlightGeometryMemory::admits(memory,budget));
        deferBuild("build-deferred",memory,delay,&budget);assert(retry);
    }
};
static void reset(){tick=1000;}
int main(int argc,char** argv){
    reset();Stand storm;const auto end=tick+32063;
    for(unsigned i=1;i<=359;++i){Request r;r.id=i+1;r.camera=V(34*std::sin(float(i)),34*std::cos(float(i)),0);storm.buildWake.events.push_back({1000+uint64_t(i)*32000/359,r});}
    storm.buildWake.events.push_back({end,storm.buildWake.events.back().request,true,false});
    unsigned attempts=0;while(!storm.stopping){storm.once();++attempts;}
#ifdef NORTHLIGHT_OLD_RETRY
    assert(attempts>=359&&"BASELINE_MUST_REPRODUCE_RETRY_STORM");
    assert(storm.reports<=33&&"RETRY_LOG_THROTTLE_BYPASSED");
#else
    assert(attempts==33&&"BACKOFF_BYPASSED_BY_GI_REQUEST");
    assert(storm.reports==attempts); // one diagnostic per bounded retry
#endif
    assert(tick==end&&storm.abandoned==1&&storm.request.id==360);
    std::printf("GEOMETRY_RETRY_STORM attempts=%u notifications=359 ms=32063\n",attempts);
    reset();Stand stationary;stationary.once();assert(tick==2000&&!stationary.abandoned);
    for(unsigned reason=0;reason<5;++reason){reset();Stand interrupted;Request r;r.id=2;
        if(reason==0)r.map="Azeroth";
        if(reason==1)r.camera=V(96.01f,0,0);
        if(reason==4)r.camera=V(std::numeric_limits<float>::quiet_NaN(),0,0);
        interrupted.buildWake.events.push_back({1025,r,reason==2,reason==3});interrupted.once();
        assert(tick==1025&&interrupted.abandoned==1&&interrupted.request.id==2);
    }
    reset();Stand boundary;Request boundaryRequest;boundaryRequest.id=2;boundaryRequest.camera=V(96,0,0);
    boundary.buildWake.events.push_back({1100,boundaryRequest});boundary.once();
#ifndef NORTHLIGHT_OLD_RETRY
    assert(tick==2000&&!boundary.abandoned&&boundary.request.id==2);
#endif
    // Optional camera replay uses captured request ticks/positions verbatim.
    if(argc==2){reset();Stand captured;std::ifstream input(argv[1]);assert(input);uint64_t at,id;float x,y,z;size_t count=0;
        while(input>>at>>id>>x>>y>>z){Request r;r.id=id;r.camera=r.geometryCenter=V(x,y,z);if(!count){captured.request=r;tick=at;}else captured.buildWake.events.push_back({at,r});++count;}
        assert(count>1);const auto start=tick;const auto last=captured.buildWake.events.back();captured.buildWake.events.push_back({last.at+1000,last.request,true,false});
        unsigned retries=0;while(!captured.stopping){captured.once();assert(++retries<count+(last.at-start)/1000+2);}
        assert(captured.request.id==last.request.id);
        std::printf("GEOMETRY_RETRY_CAPTURE requests=%zu attempts=%u ms=%llu\n",count,retries,static_cast<unsigned long long>(tick-start));
    }
    std::puts("GEOMETRY_RETRY_PASS actual_worker_lambda same_region_backoff stationary_retry latest_request map_distance_stop_exit_nonfinite unchanged_memory_floor");
}
