#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightStreaming {
/* 0.3.137: one item larger than MaxBytes is accepted while nothing else is
   queued or in flight. Its bytes were already resident; the caller would
   otherwise free them synchronously on the render thread (a snapshot's shared
   BVH/plan charge exceeds 128 MiB, so every retirement used to fall back). */
/* 0.3.138: on. The 0.3.137 retire= subphase measured the synchronous
   generation free at up to 8.4 ms (p90 4.8 ms) with generationDeferred=0.
   Generation owners never park: retireOrFree uses retireGeneration (no byte cap), the
   GI worker waits at most RetireWaitMs for the reaper before a deferral. */
inline constexpr bool RetireOversizedWhenIdle=true;
inline constexpr unsigned RetireWaitMs=250;
struct RetirementStats {std::uint64_t accepted=0,oversized=0,refused=0;};
// Only immutable CPU objects. NEVER pass a D3D/COM owner to this queue.
// Bounded references and retained bytes; no allocation or destruction under
// its lock. A failed enqueue leaves the caller responsible for its reference.
class CpuRetirement {
    struct Entry {std::shared_ptr<const void> owner;size_t bytes=0;};
    static constexpr size_t Capacity=16,MaxBytes=128u<<20;
    std::array<Entry,Capacity> queue_;
    std::mutex mutex_;std::condition_variable wake_,idle_;
    size_t head_=0,count_=0,bytes_=0;bool stop_=false,inFlight_=false;
    RetirementStats stats_;
    const bool oversizedWhenIdle_;
    std::thread worker_;
    void run(){
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
        for(;;){Entry item;
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stop_||count_;});
             if(!count_&&stop_)return;
             item=std::move(queue_[head_]);head_=(head_+1)%Capacity;--count_;inFlight_=true;}
            item.owner.reset();
            {std::lock_guard<std::mutex> lock(mutex_);bytes_-=item.bytes;inFlight_=false;if(!count_)idle_.notify_all();}
        }
    }
public:
    explicit CpuRetirement(bool oversizedWhenIdle=RetireOversizedWhenIdle):oversizedWhenIdle_(oversizedWhenIdle),worker_([this]{run();}){}
    ~CpuRetirement(){ {std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}
    CpuRetirement(const CpuRetirement&)=delete;
    template<class T> bool retire(std::shared_ptr<T>& owner,size_t bytes=0){
        if(!owner)return true;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock||stop_||count_==Capacity){if(lock)++stats_.refused;return false;}
        const bool oversized=bytes_>=MaxBytes||bytes>MaxBytes-bytes_;
        if(oversized&&!(oversizedWhenIdle_&&!bytes_)){++stats_.refused;return false;}
        ++stats_.accepted;stats_.oversized+=oversized;
        // Transfer under the lock, before waking the consumer. Copying here
        // lets the worker win the race and leaves final destruction on caller.
        queue_[(head_+count_)%Capacity]={std::move(owner),bytes};++count_;bytes_+=bytes;
        lock.unlock();wake_.notify_one();return true;
    }
    /* Geometry-generation owners (BVH/plan/snapshot): the render thread would
       otherwise free them itself, so the byte cap (which bounds memory kept
       alive by DEFERRED frees) does not apply; they are already resident and
       are destroyed in queue order right after the small items ahead of them.
       Blocking lock: critical sections never destroy or allocate. Only a full
       queue refuses (caller frees synchronously). Switch off = retire(). */
    template<class T> bool retireGeneration(std::shared_ptr<T>& owner,size_t bytes=0){
        if(!oversizedWhenIdle_)return retire(owner,bytes);
        if(!owner)return true;
        std::unique_lock<std::mutex> lock(mutex_);
        if(stop_||count_==Capacity){++stats_.refused;return false;}
        const bool oversized=bytes_>=MaxBytes||bytes>MaxBytes-bytes_;
        ++stats_.accepted;stats_.oversized+=oversized;
        queue_[(head_+count_)%Capacity]={std::move(owner),bytes};++count_;bytes_+=bytes;
        lock.unlock();wake_.notify_one();return true;
    }
    RetirementStats stats(){std::lock_guard<std::mutex> lock(mutex_);return stats_;}
    /* Waits until nothing is queued and the last dequeued item is destroyed
       (bytes_ is released only after destruction). Returns true if idle. */
    bool waitIdle(unsigned milliseconds){
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return stop_||(!count_&&!inFlight_);});
    }
};
/* Render-thread parking for refused retirements. A refused snapshot may be
   the last owner of a replaced geometry generation; it is offered to the
   reaper again next frame instead of being freed inside the frame. Parking
   never exceeds four owners; a full backlog frees synchronously as before. */
class RetirementBacklog {
    using Clock=std::chrono::steady_clock;
    struct Item {std::shared_ptr<const void> owner;size_t bytes=0;Clock::time_point since{};};
    std::array<Item,4> items_;
public:
    /* Only small CPU sets/maps may park. BVH, mesh plan and snapshot owners
       (which keep a geometry generation alive for the worker's two-generation
       limit) use retireOrFree: a refusal frees them here exactly as 0.3.136.
       Parking is bounded by wall time, independent of frame rate. */
    static constexpr double MaxParkMs=250;
    std::uint64_t deferred=0,synchronous=0,generationFrees=0;
    template<class T> void retire(CpuRetirement& queue,std::shared_ptr<T>& owner,size_t bytes){
        if(!owner||queue.retire(owner,bytes))return;
        for(auto& item:items_)if(!item.owner){item.owner=std::move(owner);item.bytes=bytes;item.since=Clock::now();++deferred;return;}
        ++synchronous;owner.reset();
    }
    template<class T> void retireOrFree(CpuRetirement& queue,std::shared_ptr<T>& owner,size_t bytes){
        if(!owner||queue.retireGeneration(owner,bytes))return;
        ++generationFrees;owner.reset();
    }
    void retry(CpuRetirement& queue){if(parked())retry(queue,Clock::now());}
    void retry(CpuRetirement& queue,Clock::time_point now){
        for(auto& item:items_)if(item.owner&&!queue.retire(item.owner,item.bytes)&&
            std::chrono::duration<double,std::milli>(now-item.since).count()>=MaxParkMs){++synchronous;item.owner.reset();}
    }
    size_t parked()const{size_t n=0;for(const auto& item:items_)n+=item.owner!=nullptr;return n;}
    void clear(){for(auto& item:items_)item.owner.reset();}
};
inline CpuRetirement& cpuRetirement(){static CpuRetirement queue;return queue;}
// One frame-boundary clear is one retirement. Otherwise the first cache can
// consume the byte cap and force every following cache to free synchronously.
// Uses the queue's existing oversized-when-idle rule; busy/full/disabled queues
// still fall back on the caller. Only detached immutable CPU owners belong here.
class CpuRetirementBatch {
    struct Item {std::shared_ptr<const void> owner;size_t bytes=0;};
    struct Owners {std::array<Item,16> items;};
    CpuRetirement* queue_;std::shared_ptr<Owners> owners_;size_t count_=0,bytes_=0,queuedBytes_=0;
public:
    explicit CpuRetirementBatch(CpuRetirement* queue=nullptr)noexcept:queue_(queue){}
    CpuRetirementBatch(const CpuRetirementBatch&)=delete;
    template<class T> bool retire(std::shared_ptr<T>& owner,size_t bytes){
        if(!owner)return true;
        if(!owners_&&queue_&&!count_)try{owners_=std::make_shared<Owners>();}catch(...) {queue_=nullptr;}
        if(!owners_||count_==owners_->items.size()||bytes>size_t(-1)-bytes_)return false;
        owners_->items[count_++]={std::move(owner),bytes};bytes_+=bytes;return true;
    }
    size_t bytes()const{return bytes_;}size_t count()const{return count_;}
    size_t queuedBytes()const{return queuedBytes_;}
    bool finish(){
        if(!count_||!owners_||!queue_){owners_.reset();queue_=nullptr;return false;}
        if(queue_->retire(owners_,bytes_)){queuedBytes_=bytes_;return true;}
        // Preserve individual admissions when another retirement is in flight.
        // Rejected owners are destroyed outside the queue's lock, on this caller.
        bool allQueued=true;
        for(size_t i=0;i<count_;++i){auto& item=owners_->items[i];
            if(queue_->retire(item.owner,item.bytes))queuedBytes_+=item.bytes;
            else {allQueued=false;item.owner.reset();}}
        owners_.reset();return allQueued;
    }
};
}
