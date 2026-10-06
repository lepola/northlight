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

struct Counters {
    // Producer (game thread) side.
    Counter commands{0},bytes{0},publishes{0},oversizeDrops{0},blockRefused{0},recordNs{0};
    Counter chunkAllocs{0},blockAllocs{0},blockReuses{0};   // heap allocations vs pool hits: flat after warm-up
    // Both threads: memory in flight (chunks handed to the producer and not yet recycled, live blocks, registered shadows).
    Counter chunksLive{0},blocksLive{0},blockBytes{0};
    std::atomic<std::int64_t> shadowBytes{0};
    Counter highWaterBytes{0},highWaterDepth{0};
    // Waits of the game thread: count and nanoseconds by kind. Nested syncs never wait; they are counted separately.
    Counter backpressureWaits{0},backpressureNs{0},syncCalls{0},syncNs{0},presentWaits{0},presentNs{0},nestedSyncs{0};
    Counter consumerSleeps{0};
    Counter census[kMaxCmdIds]{};   // sync calls per command id (name via cmdName in command_stream.inl)
};

// One CSTREAM line fragment: the queue-side numbers. The caller prefixes it and appends frame data and the census.
inline int formatCounters(char* out,std::size_t size,const Counters& c){
    return std::snprintf(out,size,"cmds=%llu bytes=%llu chunks=%llu/%llu blocks=%llu(%llu B) shadowB=%lld hwB=%llu hwDepth=%llu bp=%llu/%.2fms sync=%llu/%.2fms present=%llu/%.2fms nested=%llu oversize=%llu refused=%llu",
        (unsigned long long)get(c.commands),(unsigned long long)get(c.bytes),(unsigned long long)get(c.chunksLive),(unsigned long long)get(c.chunkAllocs),
        (unsigned long long)get(c.blocksLive),(unsigned long long)get(c.blockBytes),(long long)c.shadowBytes.load(std::memory_order_relaxed),
        (unsigned long long)get(c.highWaterBytes),(unsigned long long)get(c.highWaterDepth),
        (unsigned long long)get(c.backpressureWaits),get(c.backpressureNs)/1e6,(unsigned long long)get(c.syncCalls),get(c.syncNs)/1e6,
        (unsigned long long)get(c.presentWaits),get(c.presentNs)/1e6,(unsigned long long)get(c.nestedSyncs),
        (unsigned long long)get(c.oversizeDrops),(unsigned long long)get(c.blockRefused));
}
}
