#pragma once
// GIThreads>1 (northlight-quality.ini): helper threads for independent probe
// solves. Each job writes only its own output slot and a probe depends only
// on (BVH, lighting, position, rays, per-key seed), so results are identical
// to the serial loop whatever the scheduling. The caller commits the outputs
// in index order. helpers==0 is never constructed (GIThreads=1 keeps the
// original serial loop). Portable; `init` sets the platform thread priority.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <array>
#include <vector>
#include "world_probe_progress.h"

namespace NorthlightGI {
class SolvePool {
public:
    SolvePool(unsigned helpers,std::function<void()> init){
        for(unsigned i=0;i<helpers;++i)threads_.emplace_back([this,init]{if(init)init();loop();});
    }
    ~SolvePool(){{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}wake_.notify_all();for(auto& t:threads_)t.join();}
    SolvePool(const SolvePool&)=delete;SolvePool& operator=(const SolvePool&)=delete;
    unsigned helpers()const{return unsigned(threads_.size());}
    // Runs job(0..count-1) on the caller plus the helpers; returns when all
    // have finished. The first exception of any job is rethrown here.
    template<class Job> void run(size_t count,const Job& job){
        if(!count)return;
        {std::lock_guard<std::mutex> lock(mutex_);job_=&job;invoke_=[](const void* p,size_t i){(*static_cast<const Job*>(p))(i);};count_=count;next_=0;active_=unsigned(threads_.size());error_=nullptr;++generation_;}
        wake_.notify_all();
        work(&job,invoke_);
        std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[&]{return active_==0;});
        job_=nullptr;if(error_){auto e=error_;error_=nullptr;std::rethrow_exception(e);}
    }
private:
    bool claim(size_t& index){
        index=next_.load(std::memory_order_relaxed);
        while(index<count_){if(next_.compare_exchange_weak(index,index+1,std::memory_order_relaxed))return true;}
        return false;
    }
    using Invoke=void(*)(const void*,size_t);
    void work(const void* job,Invoke invoke){
        size_t index;
        while(claim(index)){
            try{invoke(job,index);}catch(...){std::lock_guard<std::mutex> lock(mutex_);if(!error_)error_=std::current_exception();next_=count_;}
        }
    }
    void loop(){
        uint64_t seen=0;
        for(;;){
            const void* job;Invoke invoke;
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stopping_||generation_!=seen;});if(stopping_)return;seen=generation_;job=job_;invoke=invoke_;}
            if(job)work(job,invoke);
            {std::lock_guard<std::mutex> lock(mutex_);if(--active_==0)done_.notify_all();}
        }
    }
    std::vector<std::thread> threads_;
    std::mutex mutex_;std::condition_variable wake_,done_;
    const void* job_=nullptr;Invoke invoke_=nullptr;size_t count_=0;std::atomic<size_t> next_{0};unsigned active_=0;uint64_t generation_=0;
    std::exception_ptr error_;bool stopping_=false;
};
// One 8-probe group of the worker's window loop (cursor%8==0): solve the
// positions not yet in `cache` in parallel into out[j]; bit j of the result
// marks a prefetched slot. Does not touch the cache or its LRU order: the loop
// still performs every get/put in the original sequence, and a slot evicted
// within the group is solved serially exactly as before.
template<class Solve>
unsigned prefetchProbeGroup(SolvePool& pool,const ProbeCache& cache,Vec3 origin,const std::vector<unsigned>& order,
                            unsigned cursor,Solve solve,std::array<Probe,8>& out){
    unsigned n=0,slots[8];Vec3 points[8];ProbeGridKey keys[8];
    for(unsigned j=0;j<8&&cursor+j<order.size();++j){const Vec3 q=probeWindowPosition(origin,order[cursor+j]);ProbeGridKey k;
        if(probeGridKey(q,k)&&!cache.contains(k)){slots[n]=j;points[n]=q;keys[n]=k;++n;}}
    const auto job=[&](size_t i){out[slots[i]]=solve(keys[i],points[i]);};
    // A single independent solve needs no helper wake-up or completion wait.
    if(n==1)job(0);else pool.run(n,job);
    unsigned mask=0;for(unsigned i=0;i<n;++i)mask|=1u<<slots[i];return mask;
}
} // namespace NorthlightGI
