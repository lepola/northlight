#pragma once
// Detailed diagnostics: the effects wall time (renderEffects,
// the cpuEffects scope) split into contiguous buckets. Each mark(b) reads the clock once and bills the
// span since the previous mark to b, so the buckets sum to the whole (begin to end) and a mark that is
// not reached (an early return) rolls its span into the next one. The mod's draw calls (the counted
// raw DrawIndexedPrimitive/DrawPrimitive/UP calls) are billed on sampled frames only. Off:
// begin(false) and every mark() return at once, no clock read. Portable: the caller supplies the clock.
#include <array>
#include <cstdint>
namespace NorthlightEffectsBuckets {
enum Bucket : unsigned {Setup,PreCelestial,Celestial,CelestialRing,AO,Selection,Upload,Static,ShadowSetup,TerrainSelection,SunNear,SunFar,MoonNear,MoonFar,SunActor,MoonActor,ActorShadow,Point,
    LightingSetup,Lighting,GI,PointLighting,WaterSurface,Fog,Composite,Veil,Tail,Count};
inline const char* name(unsigned b){static const char* n[Count]={"setup","preCelestial","celestial","celestialRing","ao","selection","upload","static","shadowSetup","terrainSelection",
    "sunNear","sunFar","moonNear","moonFar","sunActor","moonActor","actorShadow","point","lightingSetup","lighting","gi","pointLighting","waterSurface","fog","composite","veil","tail"};return b<Count?n[b]:"?";}
constexpr unsigned MaxReads=33; // includes four cascades and the optional cooperative water surface
// A value snapshot travels with a retained slow frame. It owns no clock or
// draw-counter pointers and cannot acquire timings from a subsequent frame.
struct Snapshot {
    std::array<double,Count> ms{};
    double totalMs=0;unsigned reads=0;bool valid=false;
};
class Frame {
public:
    using ClockFn=std::int64_t(*)();
private:
    bool on_=false;ClockFn clock_=nullptr;std::int64_t start_=0,last_=0,total_=0;std::int64_t ticks_[Count]={};std::uint32_t draws_[Count]={};
    const std::uint32_t* counters_[4]={};std::uint32_t lastDraws_=0;unsigned reads_=0;
    std::uint32_t draws()const{std::uint32_t n=0;for(auto* c:counters_)if(c)n+=*c;return n;}
public:
    // counters: the raw draw-call counters (null entries allowed).
    void begin(bool on,ClockFn clock,const std::uint32_t* const* counters=nullptr,unsigned count=0){
        on_=on&&clock;reads_=0;total_=0;if(!on_)return;clock_=clock;
        for(auto& t:ticks_)t=0;for(auto& d:draws_)d=0;for(auto& c:counters_)c=nullptr;
        for(unsigned i=0;i<count&&i<4;++i)counters_[i]=counters[i];
        lastDraws_=draws();reads_=1;start_=last_=clock_();total_=0;
    }
    bool on()const{return on_;}
    // The span since the previous mark belongs to `ended`.
    void mark(Bucket ended){if(!on_)return;const std::int64_t now=clock_();++reads_;ticks_[ended]+=now-last_;last_=now;const auto n=draws();draws_[ended]+=n-lastDraws_;lastDraws_=n;}
    // The rest (state restore, anything after the last mark) is Tail; the frame is then closed.
    void end(){if(!on_)return;mark(Tail);total_=last_-start_;on_=false;}
    std::int64_t ticks(unsigned b)const{return b<Count?ticks_[b]:0;}
    std::uint32_t drawCalls(unsigned b)const{return b<Count?draws_[b]:0;}
    std::int64_t total()const{return total_;}
    unsigned reads()const{return reads_;}
    Snapshot snapshot(std::int64_t frequency)const{
        Snapshot out;
        if(on_||reads_<2||frequency<=0)return out;
        const double scale=1000.0/double(frequency);
        for(unsigned b=0;b<Count;++b)out.ms[b]=double(ticks_[b])*scale;
        out.totalMs=double(total_)*scale;out.reads=reads_;out.valid=true;
        return out;
    }
};
}
