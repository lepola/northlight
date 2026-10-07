#pragma once
// 0.3.198 (rain): finds the game's weather particle textures without touching the game thread. Every game
// texture is created through the wrapped Device (stream mode: the replay thread's Device::CreateTexture), so
// the creation signature is all the detection needs: the art layer's procedural rain/snow textures are
// uncompressed ARGB strips (RainDrop01/RainDropRed01 32x512 = 1:16, SnowFlake01 32x64 = 1:2; texture-quality settings
// may halve the dimensions, the aspect stays). The client may create an uncompressed BLP as any of the ARGB family
// (A8R8G8B8, X8R8G8B8, A1R5G5B5, A4R4G4B4), so all are accepted; a game test showed the 32x512 texture was never
// matched as A8R8G8B8. Rain 1:16 is rare among decoded game textures; 1:2 ARGB (palette and
// uncompressed BLPs too) is common, hence the larger snow table. `WEATHER shape` lines log every create of the rain or
// snow aspect in any format (capped per kind), so a miss shows the real format and size. The draw hook compares the bound stage-0 texture (the mirror's raw
// pointer) with `hot` - one pointer comparison, no peek, lock, Get* or hash lookup.
// Table: kRainSlots/kSnowSlots slots per kind (a new candidate can only replace an entry of its own kind, preferring
// one that never drew), fixed array, no allocation. `hot` rotates on every frame in which it counted no draws, so a
// lookalike that draws nothing never holds it. Known limitation: a lookalike of the same signature that draws every
// frame can hold `hot`; mitigated by the rare rain aspect and the WEATHER candidate log. All members are touched
// under the Device's gate only.
#include <cstdint>
#include <cstdio>
#include "weather_state.h"

namespace NorthlightWeatherDetect {
using NorthlightWeather::Kind;
constexpr unsigned kRainSlots=2,kSnowSlots=4,kCandidates=kRainSlots+kSnowSlots,kTallLogs=8,kShapeLogs=32;
/* D3DFMT_* of the uncompressed ARGB family (static_asserts in renderer.cpp) */
constexpr std::uint32_t kFmtA8R8G8B8=21,kFmtX8R8G8B8=22,kFmtA1R5G5B5=25,kFmtA4R4G4B4=26;
constexpr std::uint32_t kMaxWidth=32;

inline bool isArgbFamily(std::uint32_t fmt){return fmt==kFmtA8R8G8B8||fmt==kFmtX8R8G8B8||fmt==kFmtA1R5G5B5||fmt==kFmtA4R4G4B4;}
// The aspect alone (any format, any width): the diagnostic log's filter.
inline Kind shapeOf(std::uint32_t w,std::uint32_t h){
    if(!w)return Kind::None;
    if(h==16*w)return Kind::Rain;
    if(h==2*w&&w<=kMaxWidth)return Kind::Snow;
    return Kind::None;
}

inline Kind classify(std::uint32_t w,std::uint32_t h,std::uint32_t fmt){
    if(!isArgbFamily(fmt)||!w||w>kMaxWidth)return Kind::None;
    if(w!=8&&w!=16&&w!=32)return Kind::None;
    if(h==16*w)return Kind::Rain;
    if(h==2*w)return Kind::Snow;
    return Kind::None;
}

class Detector{
public:
    struct Candidate{const void* raw=nullptr;Kind kind=Kind::None;bool drawn=false;};
    using Sink=void(*)(const char* line);
    const void* hot=nullptr;Kind hotKind=Kind::None; /* the per-draw comparison target */
    std::uint32_t generation=0,overflows=0,tallSeen=0,rainShapeSeen=0,snowShapeSeen=0;
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
        const Kind kind=classify(w,h,fmt),shape=shapeOf(w,h);
        if(shape!=Kind::None){
            std::uint32_t& seen=shape==Kind::Rain?rainShapeSeen:snowShapeSeen;
            if(seen<kShapeLogs)say("WEATHER shape kind=%s w=%u h=%u fmt=%u levels=%u matched=%d",NorthlightWeather::kindName(shape),w,h,fmt,levels,kind!=Kind::None);
            ++seen;
        }
        if(kind==Kind::None)return;
        unsigned same=0,victim=n;
        for(unsigned i=0;i<n;++i)if(table[i].kind==kind){++same;if(victim==n||(table[victim].drawn&&!table[i].drawn))victim=i;} /* oldest never-drawn of the kind, else the oldest */
        if(same>=(kind==Kind::Rain?kRainSlots:kSnowSlots)){++overflows;dropAt(victim);}
        table[n++]={raw,kind,false};++generation;
        if(!hot&&!off){hot=raw;hotKind=kind;}
        say("WEATHER candidate kind=%s %ux%u fmt=%u levels=%u raw=%p generation=%u",NorthlightWeather::kindName(kind),w,h,fmt,levels,raw,generation);
    }
    void forget(const void* raw){if(raw)remove(raw);}
    void reset(){if(n)++generation;n=0;hot=nullptr;hotKind=Kind::None;}
    // Frame end: hot counted draws -> it stays and is marked drawn; otherwise try the next candidate.
    void endFrame(bool counted){
        if(counted&&hot){for(unsigned i=0;i<n;++i)if(table[i].raw==hot)table[i].drawn=true;return;}
        rotate();
    }
    // Next candidate (hot's slot + 1, wrapping).
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
