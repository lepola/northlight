#pragma once
// 0.3.192 (CS): counters of the command stream. Relaxed atomics only: they are diagnostics, never synchronization.
// Single-writer counters use own() (load+store, no locked instruction); counters both threads write use add().
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace NorthlightStream {
constexpr std::size_t kMaxCmdIds=512;   // size of the sync census; command_stream.inl static_asserts Cmd::Count fits
using Counter=std::atomic<std::uint64_t>;
inline void add(Counter& c,std::uint64_t n=1){c.fetch_add(n,std::memory_order_relaxed);}
inline void own(Counter& c,std::uint64_t n=1){c.store(c.load(std::memory_order_relaxed)+n,std::memory_order_relaxed);}
inline void raiseMax(Counter& c,std::uint64_t v){auto o=c.load(std::memory_order_relaxed);while(v>o&&!c.compare_exchange_weak(o,v,std::memory_order_relaxed)){}}
inline std::uint64_t get(const Counter& c){return c.load(std::memory_order_relaxed);}

// 0.3.193 (CS): three groups on separate cache lines (kLine: 128 B covers Apple Silicon's 128 and x86's 64): the game thread's
// counters, the replay thread's, and the few both threads write. A counter that one thread bumps per call used to share a line with
// the other thread's, so every bump of one stole the line from the other. A counter belongs to the group of its HOT writer; the
// rare cross-thread writers (add()) stay correct because add() is atomic.
constexpr std::size_t kLine=128;
struct Counters {
    // ---- Producer (game thread) ----
    alignas(kLine) Counter commands{0};   // (alignas binds to the one declarator it precedes: every group starts on its own line)
    Counter bytes{0},publishes{0},oversizeDrops{0},blockRefused{0},recordNs{0};
    Counter chunkAllocs{0},blockAllocs{0},blockReuses{0};   // heap allocations vs pool hits: flat after warm-up
    Counter highWaterBytes{0},highWaterDepth{0};
    // Waits of the game thread: count and nanoseconds by kind. Nested syncs never wait; they are counted separately.
    Counter backpressureWaits{0},backpressureNs{0},syncCalls{0},syncNs{0},presentWaits{0},presentNs{0},nestedSyncs{0};
    // Stream proxies / state / locks (0.3.192 CS, M2). Pass-through locks by reason; see PassReason in stream_proxies.h.
    static constexpr std::size_t kPassReasons=8;
    Counter passThrough[kPassReasons]{};
    Counter qiMisses{0},foreignEntries{0},foreignPointers{0};
    Counter shadowRefused{0},shadowRefusedBytes{0},shadowLate{0};   // DYNAMIC buffers refused a shadow at creation; shadows granted later at a DISCARD lock
    Counter lockRecordedBytes{0},wholeLockBytes{0};   // bytes copied into the queue by shadow/staged unlocks; the part from whole-buffer locks (size 0)
    // Per-level texture shadows (own cap, outside the queue budget): locks served from a shadow, shadows made from a fresh
    // lock (nothing to read back) or from one synchronous readback, refusals by the cap.
    Counter texShadowEvicted{0},texShadowFreshUseful{0},texShadowHits{0},texShadowFresh{0},texShadowReadbacks{0},texShadowRefused{0},texShadowRefusedBytes{0};
    // The game thread's own time per frame (Present to Present, minus its sync and backpressure waits), in ns, and its frames.
    Counter gameNs{0},gameWaitNs{0},gameFrames{0};
    Counter filteredCalls{0};   // redundant Sets the game side did not record
    Counter stateAnswered{0},stateSynced{0},lockAsync{0};
    Counter census[kMaxCmdIds]{};   // sync calls per command id (name via cmdName in command_stream.inl)
    // ---- Consumer (replay thread) ----
    alignas(kLine) Counter consumerSleeps{0};
    Counter directCalls{0};   // replayed straight on the extension device
    Counter queryPolls{0},deadCreates{0},createFailures{0},replayFailures{0},syncOnlySlots{0},proxyMismatch{0};   // replayFailures: the game thread adds rarely too
    // ---- Both threads write: memory in flight (chunks handed to the producer and not yet recycled, live blocks, registered shadows). ----
    alignas(kLine) Counter chunksLive{0};
    Counter blocksLive{0},blockBytes{0};
    std::atomic<std::int64_t> shadowBytes{0},texShadowBytes{0};
};
static_assert(alignof(Counters)==kLine&&sizeof(Counters)%kLine==0,"Counters groups are line-aligned");
static_assert(offsetof(Counters,commands)/kLine!=offsetof(Counters,consumerSleeps)/kLine&&offsetof(Counters,consumerSleeps)/kLine!=offsetof(Counters,chunksLive)/kLine,"counter groups on distinct lines");
static_assert(offsetof(Counters,consumerSleeps)%kLine==0&&offsetof(Counters,chunksLive)%kLine==0&&offsetof(Counters,commands)%kLine==0,"each group starts on a line");

// One CSTREAM line fragment: the queue-side numbers. The caller prefixes it and appends frame data and the census.
inline int formatCounters(char* out,std::size_t size,const Counters& c){
    return std::snprintf(out,size,"cmds=%llu bytes=%llu chunks=%llu/%llu blocks=%llu(%llu B) shadowB=%lld lockB=%llu wholeLockB=%llu hwB=%llu hwDepth=%llu bp=%llu sync=%llu present=%llu nested=%llu oversize=%llu refused=%llu",
        (unsigned long long)get(c.commands),(unsigned long long)get(c.bytes),(unsigned long long)get(c.chunksLive),(unsigned long long)get(c.chunkAllocs),
        (unsigned long long)get(c.blocksLive),(unsigned long long)get(c.blockBytes),(long long)c.shadowBytes.load(std::memory_order_relaxed),(unsigned long long)get(c.lockRecordedBytes),(unsigned long long)get(c.wholeLockBytes),
        (unsigned long long)get(c.highWaterBytes),(unsigned long long)get(c.highWaterDepth),
        (unsigned long long)get(c.backpressureWaits),(unsigned long long)get(c.syncCalls),
        (unsigned long long)get(c.presentWaits),(unsigned long long)get(c.nestedSyncs),
        (unsigned long long)get(c.oversizeDrops),(unsigned long long)get(c.blockRefused));
}
}
