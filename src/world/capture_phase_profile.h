#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#ifndef _WIN32
#include <chrono>
#endif

// Allocation-free CPU diagnostics. A null sink performs no clock reads. The
// caller chooses a bounded sample; durations describe only that sample and are
// never automatically extrapolated to the whole frame. Requires the platform's
// LARGE_INTEGER/QueryPerformanceCounter declarations on Windows, or a test clock.
namespace NorthlightCapturePhases {
struct PerformanceClock {
#ifdef _WIN32
    static std::int64_t read(){LARGE_INTEGER now={};QueryPerformanceCounter(&now);return now.QuadPart;}
#else
    static std::int64_t read(){return std::chrono::steady_clock::now().time_since_epoch().count();}
#endif
};
template<std::size_t N> struct Stats {
    std::array<std::int64_t,N> ticks{};
    unsigned clockReads=0;
    void clear(){ticks.fill(0);clockReads=0;}
};
template<std::size_t N,class Clock=PerformanceClock> class Scope {
    Stats<N>* sink_=nullptr;
    std::size_t phase_=0;
    std::int64_t previous_=0;
    std::int64_t now(){++sink_->clockReads;return Clock::read();}
public:
    explicit Scope(Stats<N>* sink,std::size_t phase=0):sink_(sink),phase_(phase){if(sink_)previous_=now();}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    ~Scope(){stop();}
    void next(std::size_t phase){if(sink_){const auto t=now();sink_->ticks[phase_]+=t-previous_;previous_=t;phase_=phase;}}
    void stop(){if(sink_){const auto t=now();sink_->ticks[phase_]+=t-previous_;sink_=nullptr;}}
};
}
