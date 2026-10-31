#pragma once
// 0.3.200 (gpu budget): GpuBudgetMs. A pure CPU controller (no device, no clock): the renderer feeds it Northlight's measured GPU time per
// frame (gpu_frame_timer.h, read back a few frames late) and the seconds since the previous sample; it returns a quality level. Level 0 is
// the full image, exactly as with GpuBudgetMs=0; higher levels only remove work from the heaviest passes (see the level map below).
// Hysteresis: one step down after the smoothed time stayed over the budget for DownSeconds, one step up only after it stayed under
// UpFraction of the budget for upHold seconds; a step down soon after a step up doubles upHold (up to UpHoldMax), so a scene that sits
// on the edge settles on the lower level instead of switching every few seconds.
#include <cmath>

namespace NorthlightGpuBudget {
constexpr unsigned MaxLevel=3;
constexpr float SmoothSeconds=.3f;    // exponential smoothing time constant of the measured ms
constexpr float DownSeconds=.5f;      // smoothed > budget this long: one level down
constexpr float UpFraction=.75f;      // "well under": smoothed < 75 % of the budget
constexpr float UpHoldBase=2.f,UpHoldMax=32.f; // seconds well under before one level up; doubled after an oscillation
constexpr float SettleSeconds=.25f;   // after a change: the readback still shows the old level (a few frames late), counters wait
constexpr float BounceSeconds=5.f;    // a step down within this long after a step up counts as an oscillation
constexpr float CalmSeconds=30.f;     // a step up that holds this long forgets the oscillation backoff
constexpr float MaxSampleMs=250.f;    // larger readings (device stalls, loading) are ignored like invalid ones
constexpr float SpikeFactor=1.5f;     // a reading counts as at most this many budgets: one hitch (even after a long gap) cannot step the level down
constexpr float MaxDt=.25f;           // a long gap between samples counts as this much
// task 45: the GPU is the frame's limit only when the replay thread waits for it before Present (renderer.cpp waitPreviousFrameGpu, command stream
// with framesAhead>=2). A smoothed wait over this many ms per frame counts as GPU-bound (measured: hundreds of ms per 120 frames when GPU-bound, ~0 when CPU-bound).
constexpr float BoundWaitMs=.5f;
// task 45: the same threshold for the game thread's wait for the replay thread (Present + backpressure waits per frame): above it the replay thread is the limit.
// task 45: one frame's wait counts as at most this many ms. A single hitch (a 10-30 ms backpressure wait while a large texture uploads) kept the smoothed
// wait over BoundWaitMs for more than DownSeconds and stepped the CPU budget down in game although the game thread never otherwise waited; only a wait
// on most frames (bound needs about a quarter of the frames at the cap) now counts.
constexpr float MaxWaitMs=2.f;

// task 45: a per-frame wait (ms, 0 on a frame that did not wait) smoothed like the controller's time: the renderer's GPU latency wait (GPU-bound) and the
// game thread's wait for the replay thread (replay-bound). Callers gate on whether the wait is measured at all.
struct WaitSignal {
    float smoothed=0;bool primed=false;
    void reset(){*this=WaitSignal{};}
    void update(float waitMs,float dt){
        if(!std::isfinite(waitMs)||waitMs<0)waitMs=0;
        if(waitMs>MaxWaitMs)waitMs=MaxWaitMs;
        dt=std::isfinite(dt)&&dt>0?(dt<MaxDt?dt:MaxDt):0.f;
        if(!primed){smoothed=waitMs;primed=true;}else if(waitMs!=smoothed)smoothed+=(waitMs-smoothed)*(1.f-std::exp(-dt/SmoothSeconds));}
    bool bound()const{return smoothed>BoundWaitMs;}
};

struct Controller {
    float smoothed=0,over=0,under=0,settle=0,sinceUp=1e9f,upHold=UpHoldBase;
    unsigned level=0,changes=0;bool primed=false,probing=false; // probing: the last change was a step up (a step down within BounceSeconds is a bounce)
    void reset(){*this=Controller{};}
    // budgetMs 0: off, level 0 and the state forgotten. A non-finite, non-positive or huge ms is ignored (level kept, nothing advances).
    // task 45: lighten=false (the frame is not limited by what this budget measures) blocks stepping down (over stays 0). 0.3.217: it also
    // counts toward stepping up, like "well under the budget": a cut that buys no frame time is given back one level per upHold (the same
    // oscillation backoff applies when the restored level makes this the limit again). Before, a scene that sat over the budget but was
    // not GPU-bound kept the lowest level for good. The smoothed time still follows the readings. The default keeps the earlier behaviour.
    unsigned update(float budgetMs,float ms,float dt,bool lighten=true){
        if(!(budgetMs>0)){reset();return level;}
        if(!std::isfinite(ms)||!(ms>0)||ms>MaxSampleMs)return level;
        dt=std::isfinite(dt)&&dt>0?(dt<MaxDt?dt:MaxDt):0.f;
        if(ms>budgetMs*SpikeFactor)ms=budgetMs*SpikeFactor;
        if(!primed){smoothed=ms;primed=true;}
        else smoothed+=(ms-smoothed)*(1.f-std::exp(-dt/SmoothSeconds));
        sinceUp+=dt;
        if(probing&&sinceUp>=CalmSeconds){probing=false;upHold=UpHoldBase;}
        if(settle>0){settle-=dt;over=under=0;return level;}
        over=lighten&&smoothed>budgetMs?over+dt:0.f;
        under=smoothed<budgetMs*UpFraction||!lighten?under+dt:0.f;
        if(over>=DownSeconds&&level<MaxLevel){
            if(probing&&sinceUp<BounceSeconds)upHold=upHold*2<UpHoldMax?upHold*2:UpHoldMax;
            probing=false;++level;changed();
        }else if(under>=upHold&&level>0){--level;sinceUp=0;probing=true;changed();}
        return level;
    }
private:
    void changed(){over=under=0;settle=SettleSeconds;++changes;}
};

// Level map. Every function returns the full-quality value at level 0.
// FogClouds march intervals over the same 128 units: 40 (full), 32 (level 1), 24 (level 2+).
inline unsigned cloudSteps(unsigned level){return level==0?40u:level==1?32u:24u;}
// WorldFog march intervals: 48 (full), 40 (level 3).
inline unsigned fogSteps(unsigned level){return level>=3?40u:48u;}
// Lamps keep the configured limit at every budget level; active shadow slots stay lit.
inline unsigned lightLimit(unsigned limit,unsigned,unsigned shadowReserve=0){
    return std::max(limit,shadowReserve);}
// Shader constant that lengthens an interval of a march written as mad(range,1/fullSteps,delta): 0 when steps==fullSteps.
inline float spacingDelta(float range,unsigned steps,unsigned fullSteps){
    return steps==fullSteps||!steps||!fullSteps?0.f:range*(1.f/float(steps)-1.f/float(fullSteps));}
// task 45: ActorShadowCpuMs. The effective actor shadow radius (yards) at a CPU budget level; setting 0 = unlimited, so level 1 gives 30.
// Level 0 is the setting itself (0 stays 0 = no limit, the old path); levels 1..3 cap it at 30, 20 and 12 yards.
inline unsigned actorShadowRadius(unsigned setting,unsigned level){
    const unsigned cap=level==0?0u:level==1?30u:level==2?20u:12u;
    return !cap?setting:(setting&&setting<cap?setting:cap);}
} // namespace NorthlightGpuBudget
