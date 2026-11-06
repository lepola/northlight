#pragma once
#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>

// Envelope time budgets read this clock. Tests may substitute a deterministic
// clock so that budget-limited schedules are exactly reproducible.
#ifndef NORTHLIGHT_REPLAY_BOUNDS_CLOCK
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK std::chrono::steady_clock
#endif
namespace NorthlightReplayBounds {
using BoundsClock=NORTHLIGHT_REPLAY_BOUNDS_CLOCK;
/* 0.3.137 coverage switches. Scheduling/cost only: every bound that is still
   produced is bit-identical to 0.3.136, so culling stays exactly as safe.
   FastReady: a warm cheap envelope reads the clock once per ReadyClockStride
   draws instead of ~8 times per draw (the dominant evaluation cost measured),
   so the same cheap allowance covers roughly the whole crowd.
   BuildClockStride: cold source builds read the clock per N vertices, not per
   vertex. ResultMemo=false: the exact result memo hit ~6% (0.3.136 log) and
   copied 3.6 KiB keys on every store; never allocate or consult it. */
constexpr bool FastReady=true,ResultMemo=false;
constexpr unsigned ReadyClockStride=8,BuildClockStride=32;
// Lending is allowed only after both reserved passes have had their turn.
// Closing those passes prevents spending the same unused allowance twice.
struct AdaptiveTimeBudget {
    static constexpr std::uint64_t Cheap=280000,Heavy=70000,Build=150000,Total=500000;
    // Missing current-pose bounds replay once per cascade and cube face. Give
    // large captures 2 us per extra packet; heavy work keeps its own cap.
    static constexpr std::uint64_t MaxTotal=4000000;
    std::uint64_t totalLimit=Total,cheapLimit=Cheap,buildLimit=Build;
    bool reservedClosed=false;
    void reset(size_t packets=0){
        const auto extra=std::min<size_t>(packets>256?packets-256:0,(MaxTotal-Total)/2000)*2000;
        totalLimit=Total+extra;cheapLimit=Cheap+extra;buildLimit=Build;reservedClosed=false;
    }
    // After the first cheap/heavy turns, use half their unspent crowd allowance
    // to warm cold sources. The final cheap turn receives only what remains.
    void lendToBuild(std::uint64_t readyUsed,std::uint64_t heavyUsed){
        if(reservedClosed||totalLimit==Total)return;
        const auto left=readyUsed>=totalLimit||heavyUsed>=totalLimit-readyUsed?0:totalLimit-readyUsed-heavyUsed;
        buildLimit=std::max(Build,left/2);
    }
    size_t buildVertices()const{return size_t(std::min<std::uint64_t>(32768,2048*buildLimit/Build));}
    void finishReservedTurns(std::uint64_t heavyUsed,std::uint64_t buildUsed){
        if(reservedClosed)return;
        reservedClosed=true;
        cheapLimit=heavyUsed>=totalLimit||buildUsed>=totalLimit-heavyUsed?0:totalLimit-heavyUsed-buildUsed;
    }
    std::uint64_t lent()const{const auto reserved=totalLimit-Heavy-Build;return cheapLimit>reserved?cheapLimit-reserved:0;}
};
}
