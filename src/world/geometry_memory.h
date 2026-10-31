#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>

// Admission estimates, not a guarantee against allocations by the game/driver.
// The owner queries process address space immediately before each costly stage.
namespace NorthlightGeometryMemory {
constexpr uint64_t MiB=1024ull*1024ull;
// Keep 256 MiB for unrelated game/driver allocations. Entry adds a separate
// 256 MiB working allowance; interior/upload checks still re-sample current VA
// and enforce request-sized headroom plus the unchanged contiguous margins.
constexpr uint64_t ProcessReserve=256*MiB;
// Contiguous headroom above the largest single allocation of a stage. A 32-bit
// process whose largest free region shrank to ~20 MiB still reported 2.5 GiB
// free before one silent exit; every large allocation now needs this margin.
constexpr uint64_t ContiguousMargin=32*MiB;
constexpr uint64_t ChunkMargin=16*MiB;
// Aggregate headroom required at stage entry (buildBudget).
constexpr uint64_t EntryHeadroom=256*MiB;
// Aggregate headroom kept for allocations INSIDE a build that already passed buildBudget at stage entry.
// Entry reserved the build's own headroom (EntryHeadroom); re-requiring it per allocation double-counts the
// memory the build itself consumes. Interior checks keep the process reserve plus this guard.
constexpr uint64_t InBuildHeadroom=128*MiB;
struct Sample { uint64_t available=0,largest=0;bool valid=false; };
// task 42: Wine's GlobalMemoryStatusEx ullAvailVirtual does not subtract reserve-only regions (a
// 1.9 GiB MEM_RESERVE test read 2656 MiB available with 389 MiB actually free). Every full
// VirtualQuery walk records how far the API overstates the walked free total; cheap samples that
// read only the API subtract it. On Windows the two agree within a few MiB (sampling race), so a
// difference under BiasFloor is ignored and Windows decisions stay unchanged.
constexpr uint64_t BiasFloor=64*MiB;
inline std::atomic<uint64_t>& availableBias(){static std::atomic<uint64_t> bias{0};return bias;}
// After a full walk: returns the trustworthy available value (the walked free total when the API overstates it).
inline uint64_t noteWalk(uint64_t apiAvailable,uint64_t walkedFree){
    const uint64_t over=apiAvailable>walkedFree?apiAvailable-walkedFree:0;
    availableBias().store(over>=BiasFloor?over:0,std::memory_order_relaxed);
    return over>=BiasFloor?walkedFree:apiAvailable;
}
// API-only sample: subtract the last walk's overstatement.
inline uint64_t corrected(uint64_t apiAvailable){
    const uint64_t bias=availableBias().load(std::memory_order_relaxed);
    return apiAvailable>bias?apiAvailable-bias:0;
}
struct Budget { uint64_t available=0,largest=0;bool valid=false; };
inline bool add(uint64_t a,uint64_t b,uint64_t& out){
    if(b>std::numeric_limits<uint64_t>::max()-a)return false;
    out=a+b;return true;
}
inline Budget budgetWith(uint64_t headroom,uint64_t largestAllocation){
    // Aggregate: process reserve plus headroom; contiguous: the stage's largest block plus a margin.
    Budget b;if(!add(largestAllocation,ContiguousMargin,b.largest)||!add(ProcessReserve,headroom,b.available))return {};
    b.valid=true;return b;
}
inline Budget buildBudget(uint64_t largestAllocation=0){return budgetWith(EntryHeadroom,largestAllocation);}
// Chunked local builds avoid large contiguous buffers; keep the process and interior reserves unchanged.
inline Budget chunkBuildBudget(){return budgetWith(EntryHeadroom-32*MiB,MiB);}
// Bounded recovery (including a replacement with one retained generation).
// Real x86 seven-region replays at 395 MiB leave >=301 MiB free with the old
// BVH held. Keep the 256 MiB process reserve plus a 32 MiB working guard;
// every large request is checked and a failed candidate is never published.
constexpr uint64_t ColdInBuildHeadroom=32*MiB,ColdEntryHeadroom=136*MiB;
inline Budget coldChunkBuildBudget(){return budgetWith(ColdEntryHeadroom,MiB);}
inline Budget coldInBuildBudget(uint64_t request=0){return budgetWith(request>ColdInBuildHeadroom?request:ColdInBuildHeadroom,request);}
// Interior allocation: the process reserve must survive the request itself, so a request above the guard raises the floor.
inline Budget inBuildBudget(uint64_t largestAllocation=0){
    return budgetWith(largestAllocation>InBuildHeadroom?largestAllocation:InBuildHeadroom,largestAllocation);
}
inline uint64_t roundUp(uint64_t bytes,uint64_t step){
    if(!step)return bytes;uint64_t rounded=0;
    if(!add(bytes,step-1,rounded))return bytes;return rounded-rounded%step;
}
// Growth of a DYNAMIC buffer: the driver keeps two or three renamed slices of
// the new capacity alive, and the new backing must fit one contiguous block.
inline Budget dynamicBudget(uint64_t capacityBytes){
    Budget b;uint64_t twice=0;
    if(!add(capacityBytes,capacityBytes,twice)||!add(twice,ChunkMargin,b.largest)||
       !add(twice,capacityBytes,b.available)||!add(b.available,ProcessReserve,b.available))return {};
    b.valid=true;return b;
}
constexpr uint64_t SmallPageCapacityLimit=512ull*1024ull;
// After the complete generation passed full upload admission, each bounded
// MANAGED page may use a fresh GlobalMemoryStatusEx available-VA sample instead
// of another full VirtualQuery sweep. This checks aggregate reserve only: it
// does not assert any contiguous block exists. Actual Create*Buffer failures
// must retain the old complete generation and enter the normal retry path.
// Reserve three capacities as for other growth, and debit successes from the
// shared sample before admitting another allocation in the same frame.
inline bool admitsSmallPageGrowth(bool valid,uint64_t available,uint64_t capacityBytes){
    if(!valid||!capacityBytes||capacityBytes>SmallPageCapacityLimit)return false;
    uint64_t twice=0,reserved=0,required=0;
    return add(capacityBytes,capacityBytes,twice)&&add(twice,capacityBytes,reserved)&&
           add(ProcessReserve,reserved,required)&&available>=required;
}
// A fresh sample can cover the consecutive local VB/IB allocation transaction.
// Reserve the same three-capacity allowance used by dynamicBudget after each
// successful growth; never credit a released old buffer before re-sampling.
// This is a conservative reservation estimate, not a guarantee against other
// threads or driver fragmentation. Keep the existing admission margins/checks.
inline void debitGrowth(Sample& sample,uint64_t capacityBytes){
    uint64_t twice=0,reserved=0;
    if(!sample.valid||!add(capacityBytes,capacityBytes,twice)||!add(twice,capacityBytes,reserved)){
        sample={};return;
    }
    sample.available=reserved>=sample.available?0:sample.available-reserved;
    sample.largest=reserved>=sample.largest?0:sample.largest-reserved;
}
inline Budget uploadBudget(uint64_t vertexBytes,uint64_t indexBytes,
                           uint64_t textureBytes,uint64_t largestTextureBytes){
    Budget b;uint64_t bytes=0,doubled=0;
    if(largestTextureBytes>textureBytes||!add(vertexBytes,indexBytes,bytes)||
       !add(bytes,textureBytes,bytes)||!add(bytes,bytes,doubled)||
       !add(ProcessReserve+64*MiB,doubled,b.available))return {};
    uint64_t largest=vertexBytes>indexBytes?vertexBytes:indexBytes;
    if(largestTextureBytes>largest)largest=largestTextureBytes;
    // Twice the largest resource plus a chunk margin: driver sub-allocation
    // chunks and the managed host copy both need contiguous space.
    if(!add(largest,largest,b.largest)||!add(b.largest,ChunkMargin,b.largest))return {};
    b.valid=true;return b;
}
inline bool admits(Sample sample,Budget budget){
    return sample.valid&&budget.valid&&sample.available>=budget.available&&
           sample.largest>=budget.largest;
}
// Which limit of `budget` refuses `sample` (aggregate first), for logs only; limit=="none" when admitted.
struct Refusal { const char* limit="none";uint64_t need=0; };
inline Refusal refusal(Sample sample,Budget budget){
    if(!budget.valid)return {"budget",0};
    if(!sample.valid)return {"sample",0};
    if(sample.available<budget.available)return {"available",budget.available};
    if(sample.largest<budget.largest)return {"largest",budget.largest};
    return {};
}
// A non-exact `largest` is only the witnessed lower bound of a block, never a measurement: print ">=N".
inline void formatLargestMiB(char* out,size_t size,uint64_t bytes,bool exact){
    std::snprintf(out,size,exact?"%llu":">=%llu",(unsigned long long)(bytes>>20));
}
// A build refused for memory AFTER it passed stage entry frees its work and would pass
// entry again at once: the same BVH was rebuilt every second (162 and 482 attempts in one
// player log). The refusal raises the entry floor to what that attempt would have needed
// at entry (its entry sample plus the refused check's shortfall). The raise expires after
// LiftMs, so an estimate that includes concurrent game allocations never locks builds out.
constexpr uint32_t LiftMs=30000;
constexpr uint64_t LiftCap=512*MiB; // above the plain entry budget
struct EntryLift {
    uint64_t available=0,largest=0;uint32_t since=0;bool armed=false;
    // entry: available at the refused attempt's entry; required/observed: the refused check's
    // aggregate floor and sample; requiredLargest: its contiguous floor (a refusal for want of
    // one free block has no aggregate shortfall and would otherwise retry at once). True when armed.
    bool refused(uint64_t entry,uint64_t required,uint64_t observed,uint32_t now,uint64_t requiredLargest=0){
        uint64_t floor=0;
        if(!add(entry,required>observed?required-observed:0,floor))floor=std::numeric_limits<uint64_t>::max();
        const uint64_t cap=buildBudget().available+LiftCap;
        available=floor<cap?floor:cap;largest=requiredLargest;since=now;armed=true;return true;
    }
    bool active(uint32_t now){if(armed&&uint32_t(now-since)>=LiftMs)armed=false;return armed;}
    // The entry budget with the raised aggregate and contiguous floors while the lift is active.
    Budget apply(Budget budget,uint32_t now){
        if(budget.valid&&active(now)){
            if(available>budget.available)budget.available=available;
            if(largest>budget.largest)budget.largest=largest;
        }
        return budget;
    }
    void clear(){armed=false;}
};

// Worker-owned only. Weak ownership counts real retained generations without
// prolonging them; aliases sharing a control block count as one generation.
template<class T,size_t Limit=2> class Generations {
    static_assert(Limit>0,"generation limit must be positive");
    std::array<std::weak_ptr<T>,Limit> entries_;
public:
    size_t live(){size_t count=0;for(auto& e:entries_){if(e.expired())e.reset();else ++count;}return count;}
    bool canAdmit(){return live()<Limit;}
    // Diagnostics visit only immutable completed generations. The short local
    // lease protects a concurrent final retirement without changing the limit.
    template<class Visit> void forEach(Visit&& visit)const {for(const auto& entry:entries_)if(auto owner=entry.lock())visit(*owner);}
    bool track(const std::shared_ptr<T>& value){
        if(!value)return false;
        live();
        for(const auto& e:entries_)if(!e.expired()&&!e.owner_before(value)&&!value.owner_before(e))return true;
        for(auto& e:entries_)if(e.expired()){e=value;return true;}
        return false;
    }
};
// 0.3.156 watchdog (log only): one generation-admission stall episode. The builder
// marks each deferral and clears on admission; the render thread reports once per
// episode that outlives `ms`. The owner guards it with its own mutex.
struct StallWatch {
    // 0.3.157: an explicit armed flag (since=now|1 could lie one tick in the future: now-since wrapped to
    // 0xFFFFFFFF and the watchdog fired at once with ms=4294967295).
    uint32_t since=0;bool armed=false,reported=false;
    void deferred(uint32_t now){if(!armed){armed=true;since=now;}}
    void admitted(){armed=false;reported=false;}
    bool due(uint32_t now,uint32_t ms){if(!armed||reported||uint32_t(now-since)<ms)return false;reported=true;return true;}
};
} // namespace NorthlightGeometryMemory
