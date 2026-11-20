#pragma once

#include <d3d9.h>

#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdint>

// GPU timestamp intervals around renderer passes, read back a few frames later
// without waiting. Intervals may include CPU submission gaps and synchronization;
// they are not hardware busy-time counters. Averaged over a minute and logged.
class GpuTimer
{
public:
    enum Pass
    {
        AmbientOcclusion,
        RainRings,
        Puddles,
        LiquidSnapshot,
        SlimeCapture,
        Slime,
        Lava,
        Water,
        Fog,
        WorldDone,
        // The water's parts, inside its pass (reported on their own, not added to the total again).
        WaterSurface,
        WaterDetail,
        WaterReflections,
        WaterUnderwaterScene,
        WaterComposite,
        SkyMemory,UnderwaterView, // optional WaterWorkAudit intervals
        OwnedScene, // nested inside Water; report separately without adding to renderer total twice
        LavaStir, LavaHalo, LavaSurface, LavaGlow, // the lava's parts, nested inside Lava
        SlimeOwned, SlimeSurface, SlimeRays, SlimeShade, // the slime's parts, nested inside Slime
        kPasses,
    };
    static constexpr int kFirstPart = WaterSurface;

    // Live reporting keeps four frames. Offline bounded windows can retain all
    // samples until their existing completion query, without per-frame waits.
    explicit GpuTimer(int framesInFlight = 4) : m_frames(std::clamp(framesInFlight,4,600)) {}
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    ~GpuTimer() { Release(); }

    using Observer=std::function<void(uint64_t,bool,double,const double*)>;
    using DetailedObserver=std::function<void(uint64_t,bool,double,const double*,uint64_t)>;
    void ObserveDetailed(DetailedObserver observer){m_detailedObserver=std::move(observer);}
    static const char* Name(Pass pass);
    void Observe(Observer observer){m_observer=std::move(observer);}
    void BeginFrame(IDirect3DDevice9* dev,uint64_t tag=0);
    void EndFrame();
    bool Begin(Pass pass);
    void End(Pass pass);
    // The averages since the last report (and starts counting afresh): the whole world pass, then each pass that ran.
    std::string Report(bool detailed = false);
    // The queries go with the device's default pool, so they are let go before a Reset.
    void Release();

private:
    static constexpr int kMaxSpans = 48;
    struct Span
    {
        IDirect3DQuery9* start = nullptr;
        IDirect3DQuery9* end = nullptr;
        Pass pass = kPasses;
    };
    struct Frame
    {
        IDirect3DQuery9* disjoint = nullptr;
        IDirect3DQuery9* frequency = nullptr;
        IDirect3DQuery9* start = nullptr;
        IDirect3DQuery9* end = nullptr;
        Span spans[kMaxSpans];
        int spanCount = 0;
        bool issued = false;
        uint64_t tag=0;
    };

    bool Ensure(IDirect3DQuery9*& query, D3DQUERYTYPE type);
    void Collect(Frame& frame);

    Observer m_observer;
    DetailedObserver m_detailedObserver;
    IDirect3DDevice9* m_dev = nullptr;
    bool m_unsupported = false;
    bool m_inFrame = false;
    std::vector<Frame> m_frames;
    int m_current = 0;
    int m_open[kPasses] = {};
    double m_frameMs = 0.0;
    double m_passMs[kPasses] = {};
    unsigned m_passFrames[kPasses] = {};
    unsigned m_frameCount = 0;
    unsigned m_dropped = 0;
    unsigned m_spanOverflow = 0;
    std::vector<double> m_frameSamples;
};

// Shared scope for session and renderer passes; refused/nested starts never
// close someone else's interval. Destruction also covers failed draw paths.
class GpuScope
{
public:
    GpuScope(GpuTimer* timer,GpuTimer::Pass pass)
        :m_timer(timer),m_pass(pass),m_issued(timer && timer->Begin(pass)){}
    GpuScope(const GpuScope&)=delete;
    GpuScope& operator=(const GpuScope&)=delete;
    ~GpuScope(){if(m_issued)m_timer->End(m_pass);}
private:
    GpuTimer* m_timer;
    GpuTimer::Pass m_pass;
    bool m_issued;
};
