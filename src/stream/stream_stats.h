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

// 0.3.192 (CS): three groups on separate cache lines (kLine: 128 B covers Apple Silicon's 128 and x86's 64): the game thread's
// counters, the replay thread's, and the few both threads write. A counter that one thread bumps per call used to share a line with
// the other thread's, so every bump of one stole the line from the other. A counter belongs to the group of its HOT writer; the
// rare cross-thread writers (add()) stay correct because add() is atomic.
constexpr std::size_t kLine=128;
struct Counters {
    // ---- Producer (game thread) ----
    alignas(kLine) Counter commands{0};   // (alignas binds to the one declarator it precedes: every group starts on its own line)
    Counter bytes{0},publishes{0},oversizeDrops{0},blockRefused{0},recordNs{0};
    Counter chunksHeap{0},blockPoolBytes{0};   // chunks allocated from the heap right now (in use + pooled); bytes of idle pooled Blocks
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
    // 0.3.196 (task 12): fresh keeps skipped for lack of free room (they stage instead), and the readbacks split by why the level had no shadow: its fresh keep was evicted, its
    // re-locked shadow was evicted, its fresh keep was skipped (no room), or it never had one (a first write above the fresh limit, a refused or dropped shadow).
    Counter texShadowFreshSkipped{0},readbackAfterFreshDrop{0},readbackAfterRelockedEvict{0},readbackNeverShadowed{0},readbackAfterFreshSkip{0};   // ...Skip: the level's fresh keep was skipped (no room)
    Counter texShadowSpared{0},texShadowSpareReuses{0};   // 0.3.200 (pipeline): evicted level allocations kept as spares / reused by a new level shadow (StreamCore::texSpare)
    // 0.3.192 (CS): buffer shadows made by ONE synchronous whole-buffer readback at a write re-lock (DYNAMIC late shadows / non-DYNAMIC shadows), LRU evictions
    // by kind (hot = locked within kShadowHotFrames), re-locks refused (too big for the cap / no victim qualified), and adaptive cap growths.
    Counter dynShadowReadbacks{0},stShadowReadbacks{0},dynShadowEvicted{0},stShadowEvicted{0},hotShadowEvicted{0},relockRefused{0},relockRefusedBytes{0},shadowCapGrows{0};
    // 0.3.192 (CS): the LARGE-buffer allowance (command_queue.h LargeShadowBudgetBytes): grants (shadows made) and drops (evicted for another large buffer, pressure, GPU write).
    Counter largeShadowGrants{0},largeShadowDrops{0};
    // 0.3.204 (task 21): zero-copy buffer unlocks (see unlockBuffer): unlocks recorded as a reference into the large buffer's slice (no copy into the queue) and their bytes, DISCARD renames to the spare slice,
    // spare slices allocated for them, and the waits (count, ns) for a slice the replay thread still reads (a busy spare at a rename, or a non-DISCARD/NOOVERWRITE write lock). Game thread.
    Counter zeroCopyUnlocks{0},zeroCopyBytes{0},renames{0},renameAllocs{0},renameWaits{0},renameWaitNs{0},writeWaits{0},writeWaitNs{0};   // rename*: DISCARD renames; write*: waits of the other write locks (flags 0) for a slice's readers
    Counter waitMax{0},waitBudget{0},waitPressure{0},waitAlloc{0},maxRing{0},maxDiscards{0};   // why a DISCARD rename had to wait (ring at its allowed size / budget refused / memory pressure / allocation failed); the largest ring (non-current slices) in use; the largest per-frame DISCARD count of a buffer (reset by each zerocopy line)
    Counter zcSkipPool{0},zcSkipNoShadow{0},zcSkipSmall{0},zcSkipOther{0};   // DISCARD/NOOVERWRITE write unlocks of DYNAMIC buffers that did NOT go zero-copy, by reason (not the default pool / no shadow slice / below 4 KiB / other)
    // The game thread's own time per frame (Present to Present, minus its sync and backpressure waits), in ns, and its frames.
    Counter gameNs{0},gameWaitNs{0},gameFrames{0};
    // 0.3.204 (task 21, Diagnostics only): where the game thread's own time goes, ns (zero while Diagnostics are off). lockNs: buffer/image Lock+Unlock work (shadow memset/memcpy, readbacks),
    // synchronous waits excluded; recordSampledNs: 16 x the sampled (1 in 16) generated method bodies, waits and snapshot capture excluded; snapNs: snapshot captures at draw triggers;
    // presentBookNs: the Present call's bookkeeping, waits excluded.
    Counter lockNs{0},recordSampledNs{0},snapNs{0},presentBookNs{0};
    Counter cmdSampledNs[kMaxCmdIds]{},cmdSamples[kMaxCmdIds]{};   // the same sampled recording time per command id, and the samples (x16 = calls); the CSTREAM top line
    Counter filteredCalls{0};   // redundant Sets the game side did not record
    Counter stateAnswered{0},stateSynced{0},lockAsync{0};
    Counter coopAnswered{0};   // 0.3.204 (task 21): TestCooperativeLevel calls the game thread answered D3D_OK from StreamCore::coopState (not in stateAnswered)
    Counter census[kMaxCmdIds]{};   // sync calls per command id (name via cmdName in command_stream.inl)
    // ---- Consumer (replay thread) ----
    alignas(kLine) Counter consumerSleeps{0};
    Counter directCalls{0};   // replayed straight on the extension device
    Counter queryPolls{0},deadCreates{0},createFailures{0},replayFailures{0},syncOnlySlots{0},proxyMismatch{0};   // replayFailures: the game thread adds rarely too
    Counter skippedFrames{0},skippedCommands{0};   // 0.3.200 (frame skip): frames replayed in skip mode (no draws, no real Present) and the draws/clears they dropped
    // ---- Both threads write: memory in flight (chunks handed to the producer and not yet recycled, live blocks, registered shadows). ----
    alignas(kLine) Counter chunksLive{0};
    Counter blocksLive{0},blockBytes{0};
    std::atomic<std::int64_t> shadowBytes{0},texShadowBytes{0},largeShadowBytes{0};   // largeShadowBytes: the large allowance, NOT part of shadowBytes (shadowAdmit's cap); 0.3.204 (task 21): also the spare and the retired slices
    std::atomic<std::int64_t> retiredBytes{0},ringBytes{0},ringSlices{0};   // 0.3.204 (task 21): live bytes of retired slices (dropped while the replay thread may still read them) and of ring slices (the buffers' non-current slices; both counted in largeShadowBytes too)
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
