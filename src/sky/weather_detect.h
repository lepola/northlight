#pragma once
// 0.3.198 (rain): finds the game's weather particle textures without touching the game thread. Every game
// texture is created through the wrapped Device (stream mode: the replay thread's Device::CreateTexture), so
// the creation signature is all the detection needs: the art layer's procedural rain/snow textures are
// A8R8G8B8 strips (RainDrop01/RainDropRed01 32x512 = 1:16, SnowFlake01 32x128 = 1:4; texture-quality settings
// may halve the dimensions, the aspect stays). The draw hook then compares the bound stage-0 texture (the
// mirror's raw pointer) with `hot` - one pointer comparison, no peek, lock, Get* or hash lookup.
// Table <= 4 entries, no allocation. All members are touched under the Device's gate only.
#include <cstdint>
#include <cstdio>
#include "weather_state.h"

namespace NorthlightWeatherDetect {
using NorthlightWeather::Kind;
constexpr unsigned kCandidates=4,kTallLogs=8;
constexpr std::uint32_t kFmtA8R8G8B8=21; /* D3DFMT_A8R8G8B8 (static_assert in renderer.cpp) */
constexpr std::uint32_t kMaxWidth=32;

inline Kind classify(std::uint32_t w,std::uint32_t h,std::uint32_t fmt){
    if(fmt!=kFmtA8R8G8B8||!w||w>kMaxWidth)return Kind::None;
    if(h==16*w)return Kind::Rain;
    if(h==4*w)return Kind::Snow;
    return Kind::None;
}

class Detector{
public:
    struct Candidate{const void* raw=nullptr;Kind kind=Kind::None;};
    using Sink=void(*)(const char* line);
    const void* hot=nullptr;Kind hotKind=Kind::None; /* the per-draw comparison target */
    std::uint32_t generation=0,overflows=0,tallSeen=0;
    Sink sink=nullptr;

    unsigned count()const{return n;}
    const Candidate& candidate(unsigned i)const{return table[i];}
    bool isCandidate(const void* raw)const{for(unsigned i=0;i<n;++i)if(table[i].raw==raw)return true;return false;}
    // Mirror inactive: never hot (the draw hook's comparison reads the mirror's stage-0 slot).
    void setOff(bool off_){off=off_;if(off)hot=nullptr,hotKind=Kind::None;}
    // `raw` = the texture before mirrorResources.wrap replaced it. A freed address can come back only through here.
    void noteCreate(const void* raw,std::uint32_t w,std::uint32_t h,std::uint32_t levels,std::uint32_t fmt){
        if(!raw)return;
        remove(raw);
        if(w&&h>=4*w){if(tallSeen<kTallLogs)say("WEATHER tall w=%u h=%u fmt=%u levels=%u",w,h,fmt,levels);++tallSeen;}
        const Kind kind=classify(w,h,fmt);if(kind==Kind::None)return;
        if(n==kCandidates){++overflows;dropAt(0);} /* the new candidate replaces the oldest */
        table[n++]={raw,kind};++generation;
        if(!hot&&!off){hot=raw;hotKind=kind;}
        say("WEATHER candidate kind=%s %ux%u fmt=%u levels=%u raw=%p generation=%u",NorthlightWeather::kindName(kind),w,h,fmt,levels,raw,generation);
    }
    void forget(const void* raw){if(raw)remove(raw);}
    void reset(){if(n)++generation;n=0;hot=nullptr;hotKind=Kind::None;}
    // Frame end while no weather draw was counted and nothing is active: try the next candidate.
    void rotate(){
        if(off||!n){hot=nullptr;hotKind=Kind::None;return;}
        unsigned at=n; /* hot's slot, or n when it has none: the next slot is then 0 */
        for(unsigned i=0;i<n;++i)if(table[i].raw==hot)at=i;
        const unsigned next=at+1>=n?0:at+1;
        hot=table[next].raw;hotKind=table[next].kind;
    }
private:
    void say(const char* fmt,...)const __attribute__((format(printf,2,3))){
        if(!sink)return;char line[200];__builtin_va_list a;__builtin_va_start(a,fmt);std::vsnprintf(line,sizeof line,fmt,a);__builtin_va_end(a);sink(line);
    }
    void remove(const void* raw){for(unsigned i=0;i<n;++i)if(table[i].raw==raw){dropAt(i);++generation;return;}}
    void dropAt(unsigned i){
        if(table[i].raw==hot){hot=nullptr;hotKind=Kind::None;}
        for(unsigned j=i+1;j<n;++j)table[j-1]=table[j];--n;table[n]={};
    }
    Candidate table[kCandidates];unsigned n=0;bool off=false;
};
}
