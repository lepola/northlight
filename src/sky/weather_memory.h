#pragma once
// The game's own weather state, read from client memory (plain reads, no call, no hook): World::s_weather (0xcd7544) points at the Weather
// object (heap, allocated once at world init); its rain/snow/sand sub-objects (+0x13c/+0x140/+0x144, non-null = that type, the same test as
// Weather::GetType 0x783b60) and its smoothed storm intensity (+0x08, set by SetStormIntensity 0x784850; the particle systems get
// (intensity - .25) / .75, clamped at 0) give the kind, including sand, and the amount without the texture shapes weather_detect.h guesses
// from and without its dependence on the weatherDensity cvar. The pointer is validated, the object is read twice and never kept (the
// sub-object pointers are only tested for null, never followed). The code proofs below are the stock client's bytes; without them, or on any
// failed read or implausible field, the reading is invalid and the texture/draw detector stays the source (weather_state.h Tracker).
// The read function is injected: NorthlightWorldContext::readSelf in production (snapshot-served when one is active), a fake memory in tests.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "weather_state.h"

namespace NorthlightWeatherMemory {
using NorthlightWeather::Kind;
// WeatherMemory (northlight-quality.ini): off until the quality file says otherwise; off means no proof and no read at all, also not in the
// command-stream snapshot (game_snapshot.h captureReaders). Relaxed: a frame that sees the old value only reads or skips once more.
inline std::atomic<bool> memoryEnabled{false};
inline bool enabled(){return memoryEnabled.load(std::memory_order_relaxed);}
inline void configure(bool on){memoryEnabled.store(on,std::memory_order_relaxed);}
constexpr std::uintptr_t kPointer=0xcd7544;
constexpr std::size_t kIntensityAt=0x08,kFogAt=0x14,kDensityAt=0x28,kRainAt=0x13c,kSnowAt=0x140,kSandAt=0x144,kObjectBytes=0x148;
constexpr float kIntensityFloor=0.25f,kIntensityGain=1.3333334f; /* the game's (intensity - .25) * 1.3333334 */

struct Reading {
    bool valid=false;
    Kind kind=Kind::None;       /* Weather::GetType order: rain, snow, sand */
    float stormIntensity=0;     /* Weather::m_intensity as stored */
    float intensity=0;          /* the particle amount, 0..1 ((stormIntensity - .25) * 1.3333334) */
    float density=0,fog=0;      /* m_densityBlend, m_fogIntensity (their product is g_dnInfo.m_weatherIntensity) */
};

inline bool plausiblePointer(std::uint32_t p){return p>=0x10000&&p<0x7ffe0000&&!(p&3);}
inline bool plausibleSub(std::uint32_t p){return p==0||plausiblePointer(p);}
inline float visibleIntensity(float stormIntensity){return std::clamp((stormIntensity-kIntensityFloor)*kIntensityGain,0.f,1.f);}

// Pure decode of the Weather object's first kObjectBytes bytes.
inline bool decode(const unsigned char* object,Reading& out){
    auto f32=[&](std::size_t at){float v;std::memcpy(&v,object+at,4);return v;};
    auto u32=[&](std::size_t at){std::uint32_t v;std::memcpy(&v,object+at,4);return v;};
    const std::uint32_t rain=u32(kRainAt),snow=u32(kSnowAt),sand=u32(kSandAt);
    if(!plausibleSub(rain)||!plausibleSub(snow)||!plausibleSub(sand))return false;
    const float storm=f32(kIntensityAt),fog=f32(kFogAt),density=f32(kDensityAt);
    if(!std::isfinite(storm)||!std::isfinite(fog)||!std::isfinite(density)||storm<-0.001f||storm>1.001f||fog<-0.001f||fog>1.001f||density<-0.001f||density>1.001f)return false;
    Reading r;r.valid=true;r.kind=rain?Kind::Rain:snow?Kind::Snow:sand?Kind::Sand:Kind::None;
    r.stormIntensity=storm;r.intensity=visibleIntensity(storm);r.density=density;r.fog=fog;
    out=r;return true;
}
// The pointer twice, the object twice; any failure or difference leaves `out` untouched.
template<class Read> inline bool readState(Read read,Reading& out){
    std::uint32_t a=0,b=0;
    if(!read(kPointer,&a,4)||!plausiblePointer(a)||!read(kPointer,&b,4)||a!=b)return false;
    unsigned char first[kObjectBytes],second[kObjectBytes];
    if(!read(a,first,sizeof first)||!read(a,second,sizeof second)||std::memcmp(first,second,sizeof first))return false;
    return decode(first,out);
}

struct CodeProof {std::uintptr_t address;const unsigned char* bytes;std::size_t size;};
// Weather::GetType (0x783b60), WorldFrame's m_weatherIntensity = densityBlend * fogIntensity through the s_weather pointer (0x4f8645),
// SetStormIntensity's m_intensity store and (intensity - .25) * 1.3333334 (0x7848ba) and those two constants. Bytes of the stock client.
inline const CodeProof* proofs(std::size_t& count){
    static const unsigned char getType[]={0x33,0xc0,0x39,0x81,0x3c,0x01,0x00,0x00,0x74,0x06,0xb8,0x01,0x00,0x00,0x00,0xc3,0x39,0x81,0x40,0x01,0x00,0x00,0x74,0x06,0xb8,0x02,0x00,0x00,0x00,0xc3,
        0x39,0x81,0x44,0x01,0x00,0x00,0x74,0x05,0xb8,0x03,0x00,0x00,0x00,0xc3};
    static const unsigned char pointerUse[]={0xa1,0x44,0x75,0xcd,0x00,0xd9,0x40,0x28,0x5f,0xd8,0x48,0x14,0xd9,0x5e,0x4c};
    static const unsigned char intensityStore[]={0xd9,0x56,0x08,0xd8,0x25,0xe4,0x8c,0x9e,0x00,0xd8,0x0d,0x7c,0x98,0x9f,0x00};
    static const unsigned char quarter[]={0x00,0x00,0x80,0x3e};        /* 0.25f */
    static const unsigned char fourThirds[]={0xab,0xaa,0xaa,0x3f};     /* 1.3333334f */
    static const CodeProof values[]={{0x783b60,getType,sizeof getType},{0x4f8645,pointerUse,sizeof pointerUse},{0x7848ba,intensityStore,sizeof intensityStore},
        {0x9e8ce4,quarter,sizeof quarter},{0x9f987c,fourThirds,sizeof fourThirds}};
    count=sizeof(values)/sizeof(values[0]);return values;
}
template<class Read> inline bool proven(Read read){
    std::size_t count;const CodeProof* proof=proofs(count);
    for(std::size_t i=0;i<count;++i){unsigned char actual[64];if(proof[i].size>sizeof actual||!read(proof[i].address,actual,proof[i].size)||std::memcmp(actual,proof[i].bytes,proof[i].size))return false;}
    return true;
}

// Zone weather is independent of the camera's visible sky. Shelter belongs to
// the effect passes, not the source; otherwise looking down eventually fades it out.
inline bool counts(bool key,bool valid){return key&&valid;}

// Per frame source: the code proofs run once (code reads bypass the snapshot, see snapshot_store.h), then one validated reading per call.
class Source {
public:
    template<class Read> Reading frame(Read read){
        if(!proofRun){proofRun=true;proofOk=proven(read);}
        Reading r;
        if(!proofOk){++skipped;return r;}
        if(readState(read,r)){++reads;return r;}
        ++failures;r=Reading{};return r;
    }
    bool proofsRan()const{return proofRun;}
    bool proofsOk()const{return proofOk;}
    unsigned reads=0,failures=0,skipped=0;
private:
    bool proofRun=false,proofOk=false;
};

// The memory reading against the texture detector, which stays as the fallback: a disagreement that lasts is logged once per sort.
// memory: the reading's kind when valid, with None for an intensity of 0 (the game draws nothing then); texture: the detector's kind this frame
// (None without a counted draw). Sand has no texture detector, so a memory sand is never compared.
class CrossCheck {
public:
    enum class Result:std::uint8_t{Agree,Kind,MemoryOnly,TextureOnly};
    static constexpr unsigned kKindFrames=30,kOnlyFrames=900;
    // Returns the sort of disagreement the first time it has lasted long enough, else Agree.
    Result frame(bool memoryValid,Kind memory,Kind texture){
        if(!memoryValid||memory==Kind::Sand){streak=0;return Result::Agree;}
        Result now=Result::Agree;
        if(memory!=Kind::None&&texture!=Kind::None&&memory!=texture)now=Result::Kind;
        else if(memory!=Kind::None&&texture==Kind::None)now=Result::MemoryOnly;
        else if(memory==Kind::None&&texture!=Kind::None)now=Result::TextureOnly;
        if(now==Result::Agree||now!=last){streak=0;last=now;if(now==Result::Agree)return Result::Agree;}
        ++streak;
        const unsigned need=now==Result::Kind?kKindFrames:kOnlyFrames,bit=1u<<unsigned(now);
        if(streak<need||(logged&bit))return Result::Agree;
        logged|=bit;return now;
    }
    static const char* name(Result r){return r==Result::Kind?"kind differs":r==Result::MemoryOnly?"memory has weather, no texture draws":r==Result::TextureOnly?"texture draws, memory has none":"agree";}
private:
    Result last=Result::Agree;unsigned streak=0,logged=0;
};
}
