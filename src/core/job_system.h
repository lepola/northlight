#pragma once
// 0.3.200 (jobs): a small job system for the replay thread's D3D-free per-frame CPU work (ReplayJobs=1).
// A fixed pool of workers, clamp(cores-2,1,4) where cores excludes the replay thread (NorthlightStream::cores():
// hardware_concurrency-1 while the stream runs, so clamp(hardware_concurrency-3,1,4); the game thread and the
// GI/plan workers keep theirs), started lazily by the owner (the renderer thread inside render(), never
// DllMain). The queue is a bounded lock-free ring (one sequence word per cell); a job is a function
// pointer, an argument and its Counter, copied into the cell: no allocation per job. A Counter is the
// handle: kick() adds one, the job's end subtracts one (its last touch). wait() helps: the waiting thread
// runs queued jobs (any counter's) until its own counter is 0, it never sleeps (a yield while the last
// ones run elsewhere). A job that throws marks its counter failed and still completes. A full queue or
// a pool without workers runs the job inline in kick(). shutdown() (and the destructor) stops the
// workers after the queue is empty and runs anything left on the caller. Workers never call D3D: the
// jobs given to it are pure CPU work on frozen inputs. Portable: no D3D, no Win32.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace NorthlightJobs {
inline unsigned workerCount(unsigned cores){return cores<=3?1u:std::min(cores-2,4u);} /* clamp(cores-2,1,4); 0 (unknown) -> 1 */
constexpr unsigned MaxWorkers=4,Capacity=64; /* Capacity: a power of two; a frame kicks a handful */
class Counter {
    friend class System;
    std::atomic<int> pending_{0};std::atomic<bool> failed_{false};
public:
    Counter()=default;Counter(const Counter&)=delete;Counter& operator=(const Counter&)=delete;
    bool done()const{return pending_.load(std::memory_order_acquire)==0;}
    bool failed()const{return failed_.load(std::memory_order_acquire);}
    void clearFailure(){failed_.store(false,std::memory_order_relaxed);}
};
// Per-frame accounting (timed frames only): jobs run, ns on workers, ns run by a waiting thread, ns inside wait().
struct Stats {unsigned jobs=0,inlineJobs=0;std::int64_t workerNs=0,helpedNs=0,waitNs=0;};
class System {
    using Clock=std::chrono::steady_clock;
    struct Job {void (*fn)(void*)=nullptr;void* arg=nullptr;Counter* counter=nullptr;};
    struct Cell {std::atomic<std::size_t> sequence{0};Job job;};
    Cell cells_[Capacity];
    char pad0_[64];std::atomic<std::size_t> head_{0};char pad1_[64];std::atomic<std::size_t> tail_{0};char pad2_[64];
    std::atomic<int> queued_{0};std::atomic<unsigned> sleepers_{0};std::atomic<bool> stop_{false},timed_{false};
    std::mutex sleep_;std::condition_variable wake_;
    std::thread threads_[MaxWorkers];unsigned workers_=0;bool started_=false,startFailed_=false;
    std::atomic<unsigned> jobs_{0},inline_{0};std::atomic<std::int64_t> workerNs_{0},helpedNs_{0},waitNs_{0};
    bool push(const Job& job){
        std::size_t pos=tail_.load(std::memory_order_relaxed);Cell* cell;
        for(;;){cell=&cells_[pos&(Capacity-1)];const std::size_t seq=cell->sequence.load(std::memory_order_acquire);
            const std::intptr_t dif=std::intptr_t(seq)-std::intptr_t(pos);
            if(dif==0){if(tail_.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed))break;}
            else if(dif<0)return false; /* full */
            else pos=tail_.load(std::memory_order_relaxed);}
        cell->job=job;cell->sequence.store(pos+1,std::memory_order_release);return true;
    }
    bool pop(Job& job){
        std::size_t pos=head_.load(std::memory_order_relaxed);Cell* cell;
        for(;;){cell=&cells_[pos&(Capacity-1)];const std::size_t seq=cell->sequence.load(std::memory_order_acquire);
            const std::intptr_t dif=std::intptr_t(seq)-std::intptr_t(pos+1);
            if(dif==0){if(head_.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed))break;}
            else if(dif<0)return false; /* empty */
            else pos=head_.load(std::memory_order_relaxed);}
        job=cell->job;cell->sequence.store(pos+Capacity,std::memory_order_release);
        queued_.fetch_sub(1,std::memory_order_seq_cst);return true;
    }
    // where: 0 worker, 1 a waiting thread, 2 inline in kick(). The counter's decrement is the job's last touch.
    void run(const Job& job,unsigned where){
        const bool timed=timed_.load(std::memory_order_relaxed);const Clock::time_point start=timed?Clock::now():Clock::time_point{};
        try{job.fn(job.arg);}catch(...){job.counter->failed_.store(true,std::memory_order_release);}
        if(timed){const std::int64_t ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
            (where==0?workerNs_:helpedNs_).fetch_add(ns,std::memory_order_relaxed);}
        jobs_.fetch_add(1,std::memory_order_relaxed);if(where==2)inline_.fetch_add(1,std::memory_order_relaxed);
        job.counter->pending_.fetch_sub(1,std::memory_order_acq_rel);
    }
    void loop(){
        for(;;){
            Job job;
            if(pop(job)){run(job,0);continue;}
            bool found=false;for(unsigned spin=0;spin<64&&!found;++spin){if(queued_.load(std::memory_order_acquire)>0)found=true;else std::this_thread::yield();}
            if(found)continue;
            std::unique_lock<std::mutex> lock(sleep_);
            sleepers_.fetch_add(1,std::memory_order_seq_cst);
            wake_.wait(lock,[&]{return stop_.load(std::memory_order_seq_cst)||queued_.load(std::memory_order_seq_cst)>0;});
            sleepers_.fetch_sub(1,std::memory_order_seq_cst);
            if(stop_.load(std::memory_order_seq_cst)&&queued_.load(std::memory_order_seq_cst)<=0)return;
        }
    }
public:
    System(){for(std::size_t i=0;i<Capacity;++i)cells_[i].sequence.store(i,std::memory_order_relaxed);(void)pad0_;(void)pad1_;(void)pad2_;}
    System(const System&)=delete;System& operator=(const System&)=delete;
    ~System(){shutdown();}
    // Lazy, owner thread only. false: no worker could be created; every kick() then runs inline.
    bool start(unsigned cores){
        if(started_||startFailed_)return started_;
        const unsigned want=workerCount(cores);stop_.store(false);
        for(unsigned i=0;i<want;++i){try{threads_[i]=std::thread([this]{loop();});++workers_;}catch(...){break;}}
        started_=workers_>0;startFailed_=!started_;return started_;
    }
    bool started()const{return started_;}
    unsigned workers()const{return workers_;}
    // Timing on this frame (a diagnostics sample frame): per-job clock reads, else none.
    void timing(bool on){timed_.store(on,std::memory_order_relaxed);}
    // false: ran inline (no workers, stopped or full); the counter is complete for this job either way.
    bool kick(Counter& counter,void (*fn)(void*),void* arg){
        const Job job{fn,arg,&counter};counter.pending_.fetch_add(1,std::memory_order_relaxed);
        if(!started_||stop_.load(std::memory_order_relaxed)||!push(job)){run(job,2);return false;}
        queued_.fetch_add(1,std::memory_order_seq_cst);
        if(sleepers_.load(std::memory_order_seq_cst)){{std::lock_guard<std::mutex> lock(sleep_);}wake_.notify_one();}
        return true;
    }
    // f lives until wait(counter) returns (the caller's object, by reference).
    template<class F> bool kick(Counter& counter,F& f){return kick(counter,[](void* p){(*static_cast<F*>(p))();},&f);}
    // Help until the counter is complete. true: none of its jobs threw.
    bool wait(Counter& counter){
        if(!counter.done()){
            const bool timed=timed_.load(std::memory_order_relaxed);const Clock::time_point start=timed?Clock::now():Clock::time_point{};
            while(!counter.done()){Job job;if(pop(job))run(job,1);else std::this_thread::yield();}
            if(timed)waitNs_.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(),std::memory_order_relaxed);
        }
        return !counter.failed();
    }
    // The accounting since the last take(); resets it.
    Stats take(){Stats s;s.jobs=jobs_.exchange(0);s.inlineJobs=inline_.exchange(0);s.workerNs=workerNs_.exchange(0);s.helpedNs=helpedNs_.exchange(0);s.waitNs=waitNs_.exchange(0);return s;}
    // Owner thread. Workers drain the queue and exit; anything still queued then runs here. Idempotent.
    void shutdown(){
        if(!started_)return;
        {std::lock_guard<std::mutex> lock(sleep_);stop_.store(true,std::memory_order_seq_cst);}wake_.notify_all();
        for(unsigned i=0;i<workers_;++i)if(threads_[i].joinable())threads_[i].join();
        Job job;while(pop(job))run(job,2);
        workers_=0;started_=false;
    }
};
}
