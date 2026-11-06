#pragma once
#include "replay_bounds.h"
#include "replay_bounds_near.h"
#include "replay_bounds_schedule.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// 0.3.143: the per-frame replay bounds pass (cheap/heavy/build turns, lending,
// cursors and budgets exactly as 0.3.142) runs on one worker while the render
// thread uploads, updates static casters and draws the shadow cache. No D3D.
// Ownership during a job: the worker alone uses the bounds cache and writes
// only job-owned results; the render thread copies them to its packets at the
// first consumer. Mesh owners and prepared programs are pinned by reference,
// the camera is copied, and each packet's captured constant bank is frozen by
// protocol: the renderer never recycles or recaptures packets before joining.
namespace NorthlightReplayBoundsJob {
// false: the unchanged 0.3.142 synchronous pass at the bounds call site.
constexpr bool Async=true;
struct Item {
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> owner; // null: owned/transient packet, never bounded
    std::shared_ptr<const NorthlightReplayBounds::Prepared> prepared; // null: no program/declaration, Unsupported
    const float* constants=nullptr; // the packet's frozen capture bank
    NorthlightReplayBounds::Bounds bounds;NorthlightReplayBounds::WorkInfo work; // results
};
struct Totals {size_t cheapVisited=0,heavyVisited=0,buildVisited=0,reservedCheapVisited=0,boundVertices=0,boundOperations=0;unsigned valid=0,nearStops=0;}; /* nearStops: phases whose near walk ended on its half of the budget */
// pointCalculateReplayBounds' schedule over pre-resolved items. Metadata was
// resolved before the job, so no preparation is charged to the cheap turn.
// Cancellation (only when results will be discarded) stops between packets.
inline Totals schedule(NorthlightReplayBounds::Cache& cache,std::vector<Item>& items,const float* inverseView,const std::atomic<bool>* cancel=nullptr,NorthlightReplayBoundsNear::List* priority=nullptr){
    using WorkKind=NorthlightReplayBounds::WorkKind;using Status=NorthlightReplayBounds::Status;
    NorthlightReplayBounds::Budget cheapBudget,heavyBudget,buildBudget;
    cheapBudget.maxVertices=0;cheapBudget.maxOperations=163840;
    heavyBudget.maxVertices=0;heavyBudget.maxOperations=32768;
    buildBudget.maxVertices=2048;buildBudget.maxOperations=65536;
    Totals t;const size_t count=items.size();
    const auto live=[&]{return !cancel||!cancel->load(std::memory_order_relaxed);};
    const auto visit=[&](size_t index,unsigned phase){
        auto& p=items[index];if(p.bounds.valid)return false;
        if(phase==1&&p.work.kind!=WorkKind::Heavy)return false;
        if(phase==2&&p.work.kind!=WorkKind::Cold)return false;
        if(phase==0&&(p.work.kind==WorkKind::Heavy||p.work.kind==WorkKind::Cold||p.work.kind==WorkKind::Unsupported))return false;
        if(!p.owner||!p.prepared){p.work.kind=WorkKind::Unsupported;return false;}
        const auto& prepared=*p.prepared;const auto& mesh=*p.owner;Status status;
        if(phase==0){++t.cheapVisited;status=cache.calculateCheap(prepared,mesh,p.owner,p.constants,inverseView,cheapBudget,p.bounds,p.work);}
        else if(phase==1){++t.heavyVisited;status=cache.evaluateHeavy(prepared,mesh,p.owner,p.constants,inverseView,heavyBudget,p.bounds,p.work);}
        else{++t.buildVisited;status=cache.buildEnclosed(prepared,mesh,p.owner,p.constants,inverseView,buildBudget,p.bounds);}
        if(status==Status::Valid)++t.valid;
        return status==Status::Budget;
    };
    // The near walk of a phase: at most half of what the phase has left, in operations, vertices (build) and time, counted from where the phase stands
    // (one rule for all three caps: a pending build that already used most of the budget leaves the rotating pass half of the rest).
    const auto halfLeft=[](size_t used,size_t max){return used+(max>used?(max-used)/2:0);};
    const auto nearWalk=[&](unsigned phase,NorthlightReplayBounds::Budget& budget,auto&& phaseCan){
        if(!priority)return;
        const size_t operations=halfLeft(budget.operations,budget.maxOperations),vertices=halfLeft(budget.vertices,budget.maxVertices);
        const std::uint64_t spent=cache.phaseNanoseconds(phase),limit=cache.phaseLimit(phase),until=spent+(limit>spent?(limit-spent)/2:0);
        const auto maxOperations=budget.maxOperations,maxVertices=budget.maxVertices;
        budget.maxOperations=operations;if(maxVertices)budget.maxVertices=vertices;
        const bool stopped=NorthlightReplayBoundsNear::nearFirst(priority,phase,[&]{return phaseCan()&&cache.phaseNanoseconds(phase)<until;},[&](size_t index){return visit(index,phase);});
        budget.maxOperations=maxOperations;budget.maxVertices=maxVertices;
        if(stopped&&phaseCan())++t.nearStops;
    };
    // Every unfinished packet retains its rotating turn, including near packets
    // too expensive for their half-budget walk. Already valid bounds cost no work.
    nearWalk(0,cheapBudget,[&]{return live()&&cache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;}); /* casters near the pivot first */
    cache.readyResume(NorthlightReplaySchedule::pass(count,cache.readyStart(count),
        [&](){return live()&&cache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);}));
    nearWalk(1,heavyBudget,[&]{return live()&&cache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;});
    cache.heavyResume(NorthlightReplaySchedule::pass(count,cache.heavyStart(count),
        [&](){return live()&&cache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;},[&](size_t index){return visit(index,1);}));
    cache.lendToBuild(buildBudget);
    if(live())cache.continuePendingBuild(buildBudget);
    nearWalk(2,buildBudget,[&]{return live()&&cache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;});
    cache.buildResume(NorthlightReplaySchedule::pass(count,cache.buildStart(count),
        [&](){return live()&&cache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;},[&](size_t index){return visit(index,2);}));
    t.reservedCheapVisited=t.cheapVisited;
    cache.finishReservedTurnsAndLend();
    cheapBudget.maxOperations=262144-heavyBudget.operations-buildBudget.operations;
    // Bonus turn keeps the first turn's cursor, as in the synchronous pass.
    NorthlightReplaySchedule::pass(count,cache.readyStart(count),
        [&](){return live()&&cache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);});
    cache.settleClock();
    t.boundVertices=buildBudget.vertices;t.boundOperations=cheapBudget.operations+heavyBudget.operations+buildBudget.operations;
    return t;
}
// Render thread, before start(): clears each packet's per-frame results and
// pins its inputs. resolve(shader,declaration) runs once per run of equal pairs.
template<class Packets,class Resolve> bool gather(std::vector<Item>& items,Packets& packets,Resolve resolve,NorthlightReplayBoundsNear::List* priority=nullptr){
    try{items.resize(packets.size());}catch(...){items.clear();return false;}
    const void* shader=nullptr;const void* decl=nullptr;std::shared_ptr<const NorthlightReplayBounds::Prepared> prepared;bool resolved=false;
    for(size_t i=0;i<packets.size();++i){auto& p=*packets[i];auto& item=items[i];
        p.pointBounds={};p.boundsWork={};p.boundsPrepared.reset();
        item.owner=p.shared;item.constants=p.constants;item.bounds={};item.work={};item.prepared.reset();if(priority)priority->consider(i,p.position(),p.selfShadow);
        if(!p.shared)continue;
        if(!resolved||p.originalShader!=shader||p.decl!=decl){shader=p.originalShader;decl=p.decl;prepared=resolve(p.originalShader,p.decl);resolved=true;}
        item.prepared=prepared;
    }
    return true;
}
// Render thread, after finish(): results reach only packets whose inputs are
// the pinned ones; any other packet keeps cleared bounds and draws. Pins are
// released here, never on the worker.
template<class Packets> void apply(std::vector<Item>& items,Packets& packets,bool usable){
    usable=usable&&items.size()==packets.size();
    for(size_t i=0;i<packets.size();++i){auto& p=*packets[i];
        if(usable&&items[i].owner==p.shared&&items[i].constants==p.constants){p.pointBounds=items[i].bounds;p.boundsWork=items[i].work;}
        else{p.pointBounds={};p.boundsWork={};}
    }
    for(auto& item:items){item.owner.reset();item.prepared.reset();}
}
// One persistent thread, one job at a time. start/finish/items are render-
// thread calls; the mutex hand-off orders every item and cache access.
class Worker {
    using Clock=std::chrono::steady_clock;
    std::mutex mutex_;std::condition_variable wake_,done_;std::thread thread_;
    enum class State {Idle,Queued,Running,Done} state_=State::Idle;bool stop_=false,inFlight_=false;
    std::atomic<bool> cancel_{false};
    NorthlightReplayBounds::Cache* cache_=nullptr;std::vector<Item> items_;NorthlightReplayBoundsNear::List priority_;float inverse_[16]={};
    Clock::time_point queued_;
public:
    struct Result {Totals totals;double workerMs=0,startLagMs=0;bool failed=false,cancelled=false;};
private:
    Result result_;
    void loop(){
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stop_||state_==State::Queued;});
             if(state_!=State::Queued)return;state_=State::Running;}
            const auto start=Clock::now();Result r;
            try{r.totals=schedule(*cache_,items_,inverse_,&cancel_,priority_.on?&priority_:nullptr);}catch(...){r.failed=true;}
            const auto end=Clock::now();r.cancelled=cancel_.load(std::memory_order_relaxed);
            r.workerMs=std::chrono::duration<double,std::milli>(end-start).count();r.startLagMs=std::chrono::duration<double,std::milli>(start-queued_).count();
            {std::lock_guard<std::mutex> lock(mutex_);result_=r;state_=State::Done;}
            done_.notify_all();
        }
    }
public:
    Worker()=default;Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    ~Worker(){finish(true);{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
    bool pending()const{return inFlight_;}
    bool finished(){if(!inFlight_)return false;std::lock_guard<std::mutex> lock(mutex_);return state_==State::Done;} /* 0.3.217 (jobs): non-blocking; true only for a kicked job whose result waits for finish() */
    // Filled by the render thread before start(); read back after finish().
    std::vector<Item>& items(){return items_;}
    NorthlightReplayBoundsNear::List& nearList(){return priority_;} /* filled by gather() with the items, before start() */
    // false: no thread (creation failed); the caller runs the synchronous pass.
    bool ready(){
        if(!thread_.joinable()){try{thread_=std::thread([this]{loop();});}catch(...){return false;}}
        return true;
    }
    bool start(NorthlightReplayBounds::Cache& cache,const float* inverseView){
        if(inFlight_||!ready())return false;
        cache_=&cache;std::memcpy(inverse_,inverseView,sizeof inverse_);cancel_.store(false,std::memory_order_relaxed);
        {std::lock_guard<std::mutex> lock(mutex_);queued_=Clock::now();state_=State::Queued;}
        wake_.notify_one();inFlight_=true;return true;
    }
    // Waits for the job. cancel: results will be discarded; stop early.
    Result finish(bool cancel=false){
        if(!inFlight_)return {};
        if(cancel)cancel_.store(true,std::memory_order_relaxed);
        std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[&]{return state_==State::Done;});
        state_=State::Idle;inFlight_=false;return result_;
    }
};
}
