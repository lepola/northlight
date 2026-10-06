#pragma once
#include <atomic>
#include <cstdint>
/* 0.3.192 (DXVK3): per-frame lock volume that DXVK 3.x charges to its allocation throttle
   (d3d9_device.cpp LockBuffer :5526-5532, ThrottleAllocation :4831-4858: every DISCARD of a direct-mapped
   buffer adds the FULL buffer size to m_discardMemoryCounter; > 10 MiB forces a submit, > 30 MiB waits
   on the staging fence, counted since the last signaled submission). Relaxed atomics only: the discard
   sites run on the replay thread (or the game thread without a stream), the log tick reads. Pure
   counters, never read by a rendering decision. */
namespace NorthlightLockMeter {
enum Site {ReplayVB,ReplayIB,LiveIB,Arena,Instances,Game,SiteCount};
enum ReadClass {ReadDynamic,ReadDefaultStatic,ReadOther};
using Counter=std::atomic<std::uint64_t>;
struct State {
    Counter frameSite[SiteCount],frameStaging;                 // since the last endFrame()
    Counter ringWraps,ringFenceReuse,ringDiscards,ringShrinks,processVertices; // interval
    Counter readLocks,readBytes,readClass[3];                  // interval
    Counter frames,discardSum,discardMax,stagingSum,stagingMax,over10MiB,siteSum[SiteCount]; // interval roll-up
    std::atomic<bool> processVerticesSeen;
};
inline State& state(){static State s;return s;}
inline constexpr std::uint64_t Over10MiB=10ull<<20;
inline void discard(Site site,std::uint64_t bytes){state().frameSite[site].fetch_add(bytes,std::memory_order_relaxed);}
// A BUFFER-mode Unlock on DXVK 3.x (our fresh DEFAULT|WRITEONLY uploads) feeds the same throttle through staging.
inline void staging(std::uint64_t bytes){state().frameStaging.fetch_add(bytes,std::memory_order_relaxed);}
inline void ringWrap(bool fenceReuse,bool discarded){auto& s=state();s.ringWraps.fetch_add(1,std::memory_order_relaxed);
    if(fenceReuse)s.ringFenceReuse.fetch_add(1,std::memory_order_relaxed);if(discarded)s.ringDiscards.fetch_add(1,std::memory_order_relaxed);}
inline void ringShrink(){state().ringShrinks.fetch_add(1,std::memory_order_relaxed);}
// Returns true on the very first call (the caller logs the one-time warning when the read-back flag is on).
inline bool processVertices(){auto& s=state();s.processVertices.fetch_add(1,std::memory_order_relaxed);return !s.processVerticesSeen.exchange(true,std::memory_order_relaxed);}
inline void readBack(ReadClass cls,std::uint64_t bytes){auto& s=state();s.readLocks.fetch_add(1,std::memory_order_relaxed);
    s.readBytes.fetch_add(bytes,std::memory_order_relaxed);s.readClass[cls].fetch_add(1,std::memory_order_relaxed);}
// Classification (dynamic / defaultStatic / other) of the read-back sites: lock_meter_readback.h.
inline ReadClass classify(unsigned usage,unsigned pool){return pool!=0/*D3DPOOL_DEFAULT*/?ReadOther:(usage&0x200/*D3DUSAGE_DYNAMIC*/)?ReadDynamic:ReadDefaultStatic;}
inline void raiseMax(Counter& c,std::uint64_t v){std::uint64_t cur=c.load(std::memory_order_relaxed);while(v>cur&&!c.compare_exchange_weak(cur,v,std::memory_order_relaxed)){}}
// Closes one frame (WorldRenderer::endFrame, replay thread): rolls the frame's discard and staging volume into the interval.
inline void endFrame(){
    auto& s=state();std::uint64_t discarded=0;
    for(int i=0;i<SiteCount;++i){const std::uint64_t v=s.frameSite[i].exchange(0,std::memory_order_relaxed);discarded+=v;s.siteSum[i].fetch_add(v,std::memory_order_relaxed);}
    const std::uint64_t staged=s.frameStaging.exchange(0,std::memory_order_relaxed);
    s.frames.fetch_add(1,std::memory_order_relaxed);s.discardSum.fetch_add(discarded,std::memory_order_relaxed);s.stagingSum.fetch_add(staged,std::memory_order_relaxed);
    raiseMax(s.discardMax,discarded);raiseMax(s.stagingMax,staged);if(discarded+staged>Over10MiB)s.over10MiB.fetch_add(1,std::memory_order_relaxed);
}
struct Snapshot {
    std::uint64_t frames=0,discardSum=0,discardMax=0,stagingSum=0,stagingMax=0,over10MiB=0,site[SiteCount]={};
    std::uint64_t ringWraps=0,ringFenceReuse=0,ringDiscards=0,ringShrinks=0,processVertices=0;
    std::uint64_t readLocks=0,readBytes=0,readClass[3]={};
};
// Returns the interval since the previous call and starts the next one.
inline Snapshot takeInterval(){
    auto& s=state();Snapshot r;const auto take=[](Counter& c){return c.exchange(0,std::memory_order_relaxed);};
    r.frames=take(s.frames);r.discardSum=take(s.discardSum);r.discardMax=take(s.discardMax);r.stagingSum=take(s.stagingSum);r.stagingMax=take(s.stagingMax);r.over10MiB=take(s.over10MiB);
    for(int i=0;i<SiteCount;++i)r.site[i]=take(s.siteSum[i]);
    r.ringWraps=take(s.ringWraps);r.ringFenceReuse=take(s.ringFenceReuse);r.ringDiscards=take(s.ringDiscards);r.ringShrinks=take(s.ringShrinks);r.processVertices=take(s.processVertices);
    r.readLocks=take(s.readLocks);r.readBytes=take(s.readBytes);for(int i=0;i<3;++i)r.readClass[i]=take(s.readClass[i]);
    return r;
}
}
