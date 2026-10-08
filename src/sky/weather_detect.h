#pragma once
// 0.3.198 (rain): finds the game's weather particle textures without touching the game thread. Every game
// texture is created through the wrapped Device (stream mode: the replay thread's Device::CreateTexture), so
// the creation signature is all the detection needs: the art layer's procedural rain/snow textures are
// palettized strips with 8-bit alpha (RainDrop01/RainDropRed01 32x512 = 1:16, SnowFlake01 32x64 = 1:2; texture-quality
// settings may halve the dimensions, the aspect stays), which the client creates as A8R8G8B8 (game test: fmt=21, 10 levels).
// Only A8R8G8B8 matches: A4R4G4B4 32x64 textures are common and matched the snow shape (it churned the snow slots).
// Rain 1:16 is rare among decoded game textures; 1:2 A8R8G8B8 is common, hence the larger snow table. `WEATHER shape`
// lines log every create of the rain or snow aspect in any format (capped per kind), so a miss shows the real format and size.
// The draw hook compares the bound stage-0 texture (the mirror's raw
// pointer) with `hot` - one pointer comparison, no peek, lock, Get* or hash lookup.
// Table: kRainSlots/kSnowSlots slots per kind (a new candidate can only replace an entry of its own kind, preferring
// one that never drew), fixed array, no allocation. `hot` rotates on every frame in which it counted no draws, so a
// lookalike that draws nothing never holds it. Known limitation: a lookalike of the same signature that draws every
// frame can hold `hot`; mitigated by the rare rain aspect and the WEATHER candidate log. All members are touched
// under the Device's gate only.
// 0.3.199 (rain mist): the game's weather mist puffs (WEATHERMISTGRAINY01, SNOWMIST01: white, the shape in alpha) are alpha
// blended (SrcAlpha/InvSrcAlpha, game test) with stage 0 = texture x vertex colour, unlit; in night and storm rain that vertex colour is
// near black, so the puffs read as black balls. The art layer ships them resampled to 1:4 (128x512, A8R8G8B8 like the rain); `mist`
// holds the textures of that signature. While the frame end arms it (rain, RainBlend), the draw hook skips every draw with a mist
// texture on stage 0 (any blend: the signature is the art layer's own); snow and sand storms keep their puffs. Unarmed, the extra per-draw cost is one bool test.
// Table: kMistSlots, a full table evicts its oldest entry that never matched a mist draw (proven mists stay).
#include <cstdint>
#include <cstdio>
#include "weather_state.h"

namespace NorthlightWeatherDetect {
using NorthlightWeather::Kind;
constexpr unsigned kRainSlots=2,kSnowSlots=4,kCandidates=kRainSlots+kSnowSlots,kTallLogs=8,kShapeLogs=32,kMistSlots=8;
constexpr std::uint32_t kFmtA8R8G8B8=21; /* D3DFMT_A8R8G8B8 (static_assert in renderer.cpp): the client expands our palettized BLPs to it */
constexpr std::uint32_t kMaxWidth=32;

// The aspect alone (any format, any width): the diagnostic log's filter.
inline Kind shapeOf(std::uint32_t w,std::uint32_t h){
    if(!w)return Kind::None;
    if(h==16*w)return Kind::Rain;
    if(h==2*w&&w<=kMaxWidth)return Kind::Snow;
    return Kind::None;
}

inline Kind classify(std::uint32_t w,std::uint32_t h,std::uint32_t fmt){
    if(fmt!=kFmtA8R8G8B8||!w||w>kMaxWidth)return Kind::None;
    if(w!=8&&w!=16&&w!=32)return Kind::None;
    if(h==16*w)return Kind::Rain;
    if(h==2*w)return Kind::Snow;
    return Kind::None;
}

// 0.3.199 (rain mist): the art layer's mist puffs, 1:4 (texture-quality settings may halve them).
inline bool isMistShape(std::uint32_t w,std::uint32_t h,std::uint32_t fmt){
    return fmt==kFmtA8R8G8B8&&(w==32||w==64||w==128)&&h==4*w;
}

class Detector{
public:
    struct Candidate{const void* raw=nullptr;Kind kind=Kind::None;bool drawn=false;};
    using Sink=void(*)(const char* line);
    const void* hot=nullptr;Kind hotKind=Kind::None; /* the per-draw comparison target */
    std::uint32_t mistOverflows=0;
    std::uint32_t generation=0,overflows=0,tallSeen=0,rainShapeSeen=0,snowShapeSeen=0;
    Sink sink=nullptr;
    bool mistArmed=false; /* 0.3.199 (rain mist): set at frame end while it rains; the draw hook skips mist draws only then */

    unsigned count()const{return n;}
    const Candidate& candidate(unsigned i)const{return table[i];}
    bool isCandidate(const void* raw)const{for(unsigned i=0;i<n;++i)if(table[i].raw==raw)return true;return false;}
    unsigned mistCount()const{return mists;}
    bool isMist(const void* raw)const{for(unsigned i=0;i<mists;++i)if(mist[i]==raw)return true;return false;}
    void proveMist(const void* raw){for(unsigned i=0;i<mists;++i)if(mist[i]==raw)mistProven[i]=true;}
    // Mirror inactive: never hot (the draw hook's comparison reads the mirror's stage-0 slot).
    void setOff(bool off_){off=off_;if(off)hot=nullptr,hotKind=Kind::None,mistArmed=false;}
    // `raw` = the texture before mirrorResources.wrap replaced it. A freed address can come back only through here.
    void noteCreate(const void* raw,std::uint32_t w,std::uint32_t h,std::uint32_t levels,std::uint32_t fmt){
        if(!raw)return;
        remove(raw);
        if(w&&h>=4*w){if(tallSeen<kTallLogs)say("WEATHER tall w=%u h=%u fmt=%u levels=%u",w,h,fmt,levels);++tallSeen;}
        if(isMistShape(w,h,fmt)){
            if(mists>=kMistSlots){++mistOverflows;unsigned victim=0;for(unsigned i=0;i<mists;++i)if(!mistProven[i]){victim=i;break;}dropMist(victim);} /* the oldest unproven, else the oldest */
            mistProven[mists]=false;mist[mists++]=raw;
            say("WEATHER mist candidate %ux%u fmt=%u levels=%u raw=%p count=%u",w,h,fmt,levels,raw,mists);
            return;
        }
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
    void reset(){if(n)++generation;n=0;hot=nullptr;hotKind=Kind::None;mists=0;mistArmed=false;for(unsigned i=0;i<kMistSlots;++i)mist[i]=nullptr,mistProven[i]=false;}
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
    void remove(const void* raw){
        for(unsigned i=0;i<mists;++i)if(mist[i]==raw){dropMist(i);break;}
        for(unsigned i=0;i<n;++i)if(table[i].raw==raw){dropAt(i);++generation;return;}
    }
    void dropMist(unsigned i){for(unsigned j=i+1;j<mists;++j)mist[j-1]=mist[j],mistProven[j-1]=mistProven[j];mist[--mists]=nullptr;mistProven[mists]=false;}
    void dropAt(unsigned i){
        if(table[i].raw==hot){hot=nullptr;hotKind=Kind::None;}
        for(unsigned j=i+1;j<n;++j)table[j-1]=table[j];--n;table[n]={};
    }
    Candidate table[kCandidates];unsigned n=0;bool off=false;
    const void* mist[kMistSlots]={};bool mistProven[kMistSlots]={};unsigned mists=0;
};
}
