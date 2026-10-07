#pragma once
// 0.3.149 render-thread instrumentation, RenderProfile=1 only (northlight-quality.ini;
// 0.3.192 CS: "render thread" is the thread that runs the Device: the replay thread when the command stream is on;
// needs Diagnostics=1). Never a rendering decision. Portable: no Win32 or device
// calls; clocks are template parameters (int64 ticks from now()) so the tests
// drive them with a fake clock.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace NorthlightRenderThreadProbe {
// Stored once by WorldRenderer::loadQuality() during device creation, before the
// first frame; relaxed loads afterwards (as NorthlightDiagnostics).
inline std::atomic<bool> profileFlag{false},probeFlag{false};
inline bool profiling(){return profileFlag.load(std::memory_order_relaxed);}
inline bool probing(){return probeFlag.load(std::memory_order_relaxed);}
inline void configure(bool profile,bool probe){profileFlag.store(profile,std::memory_order_relaxed);probeFlag.store(profile&&probe,std::memory_order_relaxed);}

// Diagnostic sample frames. RenderProfile=0: frame%120==0, exactly as before.
// RenderProfile=1: frame%127==0. 127 is prime, so the samples walk through every
// phase of Near/FarShadowInterval 1..16 (120 is a multiple of 2,3,4,5,6,8: with
// NearShadowInterval=2 every old sample was the same kind of frame). The
// functional cadences (constant self-check frame%120==0, mirror audit
// frame%120==60) never use this.
constexpr unsigned SamplePeriod=120,ProfilePeriod=127;
inline bool profileFrame(unsigned frame){return frame%ProfilePeriod==0;}
inline bool sampleFrame(unsigned frame,bool profile){return profile?profileFrame(frame):frame%SamplePeriod==0;}
inline bool sampleFrame(unsigned frame){return sampleFrame(frame,profiling());}

// Nearest-rank percentiles (the value at index ceil(q*n)-1 after sorting);
// for n=120 p95 is index 113, as in NorthlightFrameIntervals. Sorts in place.
struct Summary {unsigned count=0;double mean=0,p50=0,p95=0,p99=0,max=0;};
template<class T> double rank(const T* sorted,unsigned count,unsigned percent){
    const unsigned long long scaled=(unsigned long long)percent*count;
    const unsigned r=unsigned(scaled/100+(scaled%100?1:0));
    return double(sorted[r?r-1:0]);
}
template<class T> Summary summarize(T* values,unsigned count){
    Summary s;s.count=count;if(!count)return s;
    std::sort(values,values+count);double sum=0;for(unsigned i=0;i<count;++i)sum+=double(values[i]);
    s.mean=sum/count;s.p50=rank(values,count,50);s.p95=rank(values,count,95);s.p99=rank(values,count,99);s.max=double(values[count-1]);
    return s;
}
// Fixed storage: add() drops values beyond N; take() summarizes and restarts.
template<unsigned N> class Samples {
    float values_[N]={};unsigned count_=0;
public:
    void reset(){count_=0;}
    unsigned count()const{return count_;}
    void add(double value){if(count_<N)values_[count_++]=float(value);}
    Summary take(){const Summary s=summarize(values_,count_);count_=0;return s;}
};

// Frame kinds: which directional maps this frame drew (any source). Near/Far
// ShadowInterval>1 frames that reuse both maps are None (they also skip model
// capture). Bare: effects not applied (loading, F10 off).
enum Kind:unsigned {NearFar,NearOnly,FarOnly,None,Bare,Kinds};
inline const char* kindName(unsigned k){static const char* const n[]={"near+far","near","far","none","bare","all"};return n[k<Kinds?k:Kinds];}
inline Kind kindOf(bool applied,bool nearDrawn,bool farDrawn){return !applied?Bare:nearDrawn?(farDrawn?NearFar:NearOnly):farDrawn?FarOnly:None;}
// Series: Frame = Present entry to Present entry; Effects = renderEffects;
// Finish = the extension's frame finish inside Present; Present = the real Present.
enum Series:unsigned {Frame,Effects,Finish,Present,SeriesCount};
struct Report {unsigned frames=0,excluded=0;Summary kinds[Kinds][SeriesCount];Summary all[SeriesCount];};
// Per-frame wall times. Excluded (counted, not summarized): diagnostic sample
// frames (their timing scopes and log lines), and any frame whose window saw a
// render-thread log line (the caller passes a counter), plus the next one:
// logging costs 17-20 us per line and lands in the following interval.
class FrameCostWindow {
public:
    static constexpr unsigned Capacity=2048;
private:
    float values_[SeriesCount][Capacity]={};unsigned char kind_[Capacity]={};float scratch_[Capacity]={};
    unsigned count_=0,excluded_=0;
    int64_t previous_=0;bool primed_=false;
    double pendingEffects_=0;Kind pendingKind_=Bare;bool pending_=false,pendingExcluded_=false;
    unsigned long long logs_=0;bool dirty_=true;
public:
    unsigned count()const{return count_;}
    void reset(){count_=0;excluded_=0;previous_=0;primed_=false;pending_=false;dirty_=true;}
    // The extension's frame finish: this frame's effects wall time and kind.
    void close(double effectsMs,Kind kind,bool excluded){pendingEffects_=effectsMs;pendingKind_=kind;pendingExcluded_=excluded;pending_=true;}
    // After the real Present returned: entry, after the extension's finish, after
    // Present; logs = the render thread's log line counter at that point.
    void presented(int64_t entry,int64_t finished,int64_t done,int64_t frequency,unsigned long long logs){
        const bool had=pending_,logged=logs!=logs_,previousDirty=dirty_;pending_=false;logs_=logs;dirty_=logged;
        if(frequency<=0||finished<entry||done<finished){reset();return;}
        if(!primed_||entry<=previous_||!had){previous_=entry;primed_=true;dirty_=true;return;}
        const double ms=1000.0/double(frequency);const double interval=double(entry-previous_)*ms;previous_=entry;
        if(logged||previousDirty||pendingExcluded_){++excluded_;return;}
        if(count_>=Capacity){++excluded_;return;}
        const double sample[SeriesCount]={interval,pendingEffects_,double(finished-entry)*ms,double(done-finished)*ms};
        for(unsigned s=0;s<SeriesCount;++s)values_[s][count_]=float(sample[s]);kind_[count_++]=(unsigned char)pendingKind_;
    }
    // Summarize the window and restart it (the interval priming is kept).
    Report take(){
        Report r;r.frames=count_;r.excluded=excluded_;
        for(unsigned s=0;s<SeriesCount;++s){
            for(unsigned k=0;k<Kinds;++k){unsigned n=0;for(unsigned i=0;i<count_;++i)if(kind_[i]==k)scratch_[n++]=values_[s][i];r.kinds[k][s]=summarize(scratch_,n);}
            for(unsigned i=0;i<count_;++i)scratch_[i]=values_[s][i];r.all[s]=summarize(scratch_,count_);
        }
        count_=0;excluded_=0;return r;
    }
};

// Replay loop policies (world_renderer.h submitReplay and its callers).
// Off: every hook empty (compiles away) - the loop of 0.3.148; used on every
// frame unless RenderProfile samples it or DiagReplayProbe records it.
// Split: per-span clock reads when timed (RenderProfile sample frames only; a
// secondary number, ~5 reads per draw), drawn-packet recording for the probe
// and the capture-waste count, constant call counts by type.
// StateOnly / LogicOnly: DiagReplayProbe differential modes (no draw call /
// no D3D9 call at all).
enum Bucket:unsigned {Own,State,Draw,Buckets};
enum Constant:unsigned {Floats,Bools,Ints,Constants};
struct Off {
    static constexpr bool Draws=true,Calls=true;
    void start(){}
    void mark(Bucket){}
    void constant(Constant){}
    template<class Packet> void drawn(const Packet*){}
};
struct StateOnly:Off {static constexpr bool Draws=false;};
struct LogicOnly:Off {static constexpr bool Draws=false,Calls=false;};
template<class Clock,class Packet> struct Split {
    static constexpr bool Draws=true,Calls=true;
    bool timed=false,recordFailed=false;
    std::vector<const Packet*>* record=nullptr; /* DiagReplayProbe: this pass's drawn packets */
    std::vector<const Packet*>* used=nullptr;   /* capture waste: every pass's drawn packets */
    int64_t ticks[Buckets]={},last=0;unsigned marks[Buckets]={},constants[Constants]={};
    void start(){if(timed)last=Clock::now();}
    void mark(Bucket b){if(!timed)return;const int64_t now=Clock::now();ticks[b]+=now-last;++marks[b];last=now;}
    void constant(Constant c){++constants[c];}
    void drawn(const Packet* p){
        try{if(record)record->push_back(p);if(used)used->push_back(p);}catch(...){recordFailed=true;record=nullptr;used=nullptr;}
    }
    unsigned clockReads()const{return timed?1+marks[Own]+marks[State]+marks[Draw]:0;}
};
// Mean ticks of one Clock::now() call over `reads` back-to-back reads.
template<class Clock> double clockCost(unsigned reads){
    const int64_t start=Clock::now();for(unsigned i=0;i<reads;++i)(void)Clock::now();
    return double(Clock::now()-start)/double(reads+1);
}
// 0.3.150: the smallest clockCost() of `batches` runs. One run can catch a preemption or
// a slow read (0.3.149 in game: 185-282 ns between profile frames). The replay split
// subtracts it once per span and its state spans are about one clock read long, so an
// estimate above the in-loop cost clamped stateUsPerCall to 0 in most profile frames.
template<class Clock> double clockCostMin(unsigned reads,unsigned batches){
    double best=clockCost<Clock>(reads);for(unsigned b=1;b<batches;++b)best=std::min(best,clockCost<Clock>(reads));return best;
}

// DiagReplayProbe schedule: fixed windows cycling Off, Full, Off, StateOnly, Off,
// LogicOnly, so every mode sits between two Off windows of the same session and
// scene. full-state = draw calls, state-logic = state calls, logic = own logic.
enum ProbeMode:unsigned {ProbeOff,ProbeFull,ProbeState,ProbeLogic,ProbeModes};
inline const char* probeModeName(unsigned m){static const char* const n[]={"off","full","state","logic","?"};return n[m<ProbeModes?m:ProbeModes];}
class ProbeSchedule {
    static constexpr ProbeMode Cycle[6]={ProbeOff,ProbeFull,ProbeOff,ProbeState,ProbeOff,ProbeLogic};
    uint32_t start_=0;bool started_=false;
public:
    static constexpr uint32_t WindowMs=10000;
    void reset(){started_=false;}
    // Window serial (0,1,2,... since the first call) and its mode at tick `now`.
    uint64_t window(uint32_t now){if(!started_){start_=now;started_=true;}return uint64_t(uint32_t(now-start_)/WindowMs);}
    static ProbeMode mode(uint64_t window){return Cycle[window%6];}
};
}
