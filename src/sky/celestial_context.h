#pragma once
// Read-only, fixed-build sky data. See celestial-context-proof.md for provenance.
#include "world_context.h"
#include "celestial_time_warp.h"
#include <algorithm>
#include <cstddef>

namespace NorthlightCelestial {
struct BodyRecord {
    float center[3];
    uint32_t color;       // D3DCOLOR used by the actual sky billboard.
    uint32_t texture;     // Opaque client handle: never dereferenced here.
    float size, baseSize, period;
};
static_assert(sizeof(BodyRecord)==32,"Client sky record layout");
struct Snapshot {
    float dayFraction=0, skyCamera[3]={};
    BodyRecord bodies[3]={}; // sunCenter.blp, moon.blp, moon02.blp, in that order.
    // DayNight::RenderSky's zone skybox state (m_skybox/m_skyboxWeight, m_blendSkybox[3]/weight/flags). Pointers are never dereferenced.
    uint32_t skybox=0,blendSkybox[3]={};float skyboxWeight=0,blendWeight[3]={};uint32_t blendFlags[3]={};
};
struct Body {
    float direction[3]={}, tint[3]={}, alpha=0, altitudeVisibility=0;
    float emission=1.1f;
    float nativeTint[3]={};bool nativeAppearance=false;
    float nativeAngularRadius=0;
    float angularRadius=0; // atan(size/24): native template half-size .5, orbit radius12.
};
struct Context {
    float dayFraction=0;
    Body sun, moon, secondaryMoon;
    float sunWeight=0, moonWeight=0;
    float sunColor[3]={}, moonColor[3]={}; // Same units as input terrain direct RGB. SURFACE light (shadows, GI, lighting): scaled by light_strength.
    float sunGlowColor[3]={}, moonGlowColor[3]={}; // The same source colour for the volumetric glow (fog shafts, aureole): scaled by glow_strength, own hue (profiles apply()).
    float horizonSine=0; // the camera's far edge (apparent_horizon.h), 0..-sin 3 deg; the veil's haze at the sun
    float skyboxCover=0;      // 0..1: the strongest zone skybox (full or blended) the game draws over its planets
    bool nativePlanets=true;  // RenderSky's drawObjects rule: false when a full skybox makes the game skip the sun/moon billboards
};
// Zone skybox state, mirroring DayNight::RenderSky: a skybox (m_skybox or a blend one) counts when its pointer is set and its weight finite.
inline bool skyboxActive(uint32_t pointer,float weight){return pointer>=0x10000&&std::isfinite(weight);}
inline void decodeSkybox(const Snapshot& s,float& cover,bool& nativePlanets){
    auto weight=[](uint32_t p,float w){return skyboxActive(p,w)?std::clamp(w,0.f,1.f):0.f;};
    cover=weight(s.skybox,s.skyboxWeight);nativePlanets=!(skyboxActive(s.skybox,s.skyboxWeight)&&s.skyboxWeight>.99f);
    for(int i=0;i<3;++i){
        cover=std::max(cover,weight(s.blendSkybox[i],s.blendWeight[i]));
        if(skyboxActive(s.blendSkybox[i],s.blendWeight[i])&&s.blendWeight[i]>.99f&&!s.blendFlags[i])nativePlanets=false;
    }
}
// The mode is decided once per frame, from the first context read of that frame, so the sky-phase claim and the late render() agree.
// Hysteresis: a skybox (cover > Enter, or the game skipping its planets) switches to the late discs at once; back to the planet phase
// only after cover < Leave with native planets has held for Hold ms, so a fading or blending skybox cannot flap the two draw paths.
struct FrameMode {
    static constexpr float Enter=.02f,Leave=.005f;static constexpr uint32_t Hold=250;
    bool state=false,frameDecided=false,calm=false;uint32_t calmSince=0;unsigned switches=0;
    bool decided()const{return frameDecided;}
    bool skybox()const{return frameDecided&&state;}
    bool decide(const Context& c,uint32_t now){
        if(frameDecided)return state;
        frameDecided=true;
        const bool on=!c.nativePlanets||c.skyboxCover>Enter,quiet=c.nativePlanets&&c.skyboxCover<Leave;
        if(!state){if(on){state=true;calm=false;++switches;}}
        else if(quiet){if(!calm){calm=true;calmSince=now;}else if(now-calmSince>=Hold){state=false;calm=false;++switches;}}
        else calm=false;
        return state;
    }
    void endFrame(){frameDecided=false;}
    void reset(){*this=FrameMode{};}
};
// A claimed native body is skipped only if render() ran last frame, else its late disc would not exist.
inline bool suppressNative(unsigned renderFrame,unsigned frames){return renderFrame+1==frames;}
// Late (world-depth) disc and full glare for a body: always in a skybox frame, and in a planet-phase frame for a body the
// planet phase did not cover (no early draw, not owned off screen) whose native billboard was not seen drawing (lateDisc).
// A native billboard that did draw makes lateDisc false, so the two never double up.
// The planet-phase fallback also needs positive skybox evidence in the render-time context (skyboxCover > 0: an interior SetBlendSky is
// set before RenderSky): RenderSky is skipped underwater, with m_showSky 0 or a negative view depth, and those frames show no sky at all.
// Only a confirmed insertion inside the skybox suppresses its late fallback; the ordinary planet snapshot can precede a changing skybox.
inline bool lateBody(bool skyboxMode,bool drawnEarly,bool offscreenOwned,bool lateDisc,bool skyboxEvidence,bool skyboxLayerEarly=false){return !skyboxLayerEarly&&lateDisc&&(skyboxMode||(skyboxEvidence&&!(drawnEarly||offscreenOwned)));}
inline float smooth(float x) { x=std::max(0.f,std::min(1.f,x));return x*x*(3-2*x); }
inline bool decodeBody(const BodyRecord& r,const float* camera,Body& out) {
    Body candidate;float length2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(r.center[i])||!std::isfinite(camera[i]))return false;
        candidate.direction[i]=r.center[i]-camera[i];
        length2+=candidate.direction[i]*candidate.direction[i];
    }
    // All three original sky orbit producers explicitly normalize to radius 12.
    // Reject stale/mixed-frame or uninitialized records, not merely NaNs.
    if(length2<11.9f*11.9f||length2>12.1f*12.1f || !r.texture ||
       !std::isfinite(r.size)||r.size<0||r.size>12)return false;
    const float inv=1/std::sqrt(length2);
    for(int i=0;i<3;++i)candidate.direction[i]*=inv;
    candidate.tint[0]=float((r.color>>16)&255)/255;
    candidate.tint[1]=float((r.color>>8)&255)/255;
    candidate.tint[2]=float(r.color&255)/255;
    std::copy(candidate.tint,candidate.tint+3,candidate.nativeTint);candidate.nativeAppearance=true;
    candidate.alpha=float(r.color>>24)/255;
    candidate.angularRadius=candidate.nativeAngularRadius=std::atan(r.size/24.f);
    // Renderer policy: C1 fade over the first 4.6 degrees ABOVE the horizon.
    // No below-horizon direct source, no frame history, no invented orbit.
    candidate.altitudeVisibility=smooth(candidate.direction[2]/.08f);
    out=candidate;return true;
}
inline bool decode(const Snapshot& s,const float* camera,const float* direct,Context& out) {
    if(!std::isfinite(s.dayFraction)||s.dayFraction<0||s.dayFraction>=1)return false;
    float cameraError2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(camera[i])||!std::isfinite(s.skyCamera[i])||
           !std::isfinite(direct[i])||direct[i]<0||direct[i]>8)return false;
        float delta=camera[i]-s.skyCamera[i];cameraError2+=delta*delta;
    }
    // Prevent an old sky snapshot after teleport/map change driving world light.
    if(cameraError2>40.f*40.f)return false; /* 0.3.200: several frames of travel tolerated (NorthlightWmoContext::SkyCameraTolerance) */
    Context candidate;candidate.dayFraction=s.dayFraction;
    if(!decodeBody(s.bodies[0],s.skyCamera,candidate.sun)||
       !decodeBody(s.bodies[1],s.skyCamera,candidate.moon)||
       !decodeBody(s.bodies[2],s.skyCamera,candidate.secondaryMoon))return false;
    // Artistic energy allocation, NOT a recovered Blizzard irradiance formula.
    // Original zone/time direct color is the budget; never brighten it twice.
    // Moon02 remains exposed separately; it gets no invented extra radiance.
    decodeSkybox(s,candidate.skyboxCover,candidate.nativePlanets);
    candidate.sunWeight=candidate.sun.altitudeVisibility*candidate.sun.alpha;
    candidate.moonWeight=(1-candidate.sunWeight)*candidate.moon.altitudeVisibility*candidate.moon.alpha;
    for(int i=0;i<3;++i) {
        candidate.sunColor[i]=direct[i]*candidate.sunWeight;
        candidate.moonColor[i]=direct[i]*candidate.moonWeight;
        candidate.sunGlowColor[i]=candidate.sunColor[i];candidate.moonGlowColor[i]=candidate.moonColor[i];
    }
    out=candidate;return true;
}

// Light follows what is seen of the disc: its area over the true horizon, each row weighted by the native fade
// (DNPlanet::ClipGeometry: opaque from 0.4 over the camera's height on the billboard plane 12 away, 1.9 deg, to none
// at it; the disc shader's saturate(30*ray.z/forward)). A setting body loses its light, shadows and rays evenly as it
// sinks; a hidden one gives none, and what is left stays the game's own light (authoredFill).
inline constexpr float NativeFadeSine=.4f/12;
inline float discVisibility(float elevationSine,float angularRadius){
    if(!std::isfinite(elevationSine)||!std::isfinite(angularRadius)||!(angularRadius>0))return 0;
    const float r=std::sin(angularRadius);
    if(elevationSine-r>=NativeFadeSine)return 1;
    if(elevationSine+r<=0)return 0;
    constexpr int Rows=32;float seen=0,area=0;
    for(int i=0;i<Rows;++i){
        const float v=-1+(i+.5f)*2/Rows,width=std::sqrt(1-v*v);
        seen+=width*std::clamp((elevationSine+v*r)/NativeFadeSine,0.f,1.f);area+=width;
    }
    return seen/area;
}
// Lowest elevation (sine, 6 deg) of a light direction: a grazing light would stretch shadows to infinity. At 2 deg
// the near shadow map reached so far toward the light that a moving camera pulled 150-260 model replays into it
// (4-5 ms GPU on DXVK) instead of about 30; 6 deg shadows are a third as long.
inline constexpr float MinLightElevation=.1045f;
inline void clampLightElevation(float* direction){
    if(!(direction[2]<MinLightElevation))return;
    const float h=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]),k=h>0?std::sqrt(1-MinLightElevation*MinLightElevation)/h:0.f;
    direction[0]*=k;direction[1]*=k;direction[2]=MinLightElevation;
}
// Extension appearance/energy policy, applied after reading the native placement. Native
// billboard RGBA is zone sky artwork state, not celestial irradiance: the
// midnight captures have moon z=.81 but alpha 0, 14/255 or 1 by location.
// A raw-alpha change must not switch off the disc, shadows or fog lighting.
// Keep decode() faithful to the audited records for diagnostics/native use.
inline void applyRendererPolicy(Context& c,const float* direct){
    c.sun.alpha=c.moon.alpha=1;
    c.sun.emission=3.5f;c.moon.emission=1.1f;
    const float tint[2][3]={{1,.96f,.88f},{.94f,.97f,1}};
    for(unsigned i=0;i<3;++i){c.sun.tint[i]=c.sun.nativeAppearance?c.sun.nativeTint[i]:tint[0][i];c.moon.tint[i]=tint[1][i];}
    // Each body lights the world (rays, shadows) as much as its disc is seen; the sun first, the moon takes the rest.
    c.sun.altitudeVisibility=discVisibility(c.sun.direction[2],c.sun.angularRadius);
    c.moon.altitudeVisibility=(1-c.sun.altitudeVisibility)*discVisibility(c.moon.direction[2],c.moon.angularRadius);
    c.sunWeight=c.sun.altitudeVisibility;
    c.moonWeight=c.moon.altitudeVisibility;
    for(unsigned i=0;i<3;++i){
        c.sunColor[i]=direct[i]*c.sunWeight;
        c.moonColor[i]=direct[i]*c.moonWeight;
    }
}
inline NorthlightCelestialOrbit::Result resolveRendererSky(Context& c,const float* direct){
    NorthlightCelestialOrbit::Result orbit;
    if(!std::isfinite(c.dayFraction)||c.dayFraction<0||c.dayFraction>=1)return orbit;
    // Keep the native billboard's orbit and animated horizon enlargement. Effects
    // may change its appearance, but toggling them must not move or resize it.
    std::memcpy(orbit.sun.direction,c.sun.direction,sizeof c.sun.direction);
    std::memcpy(orbit.moon.direction,c.moon.direction,sizeof c.moon.direction);
    orbit.sun.elevation=NorthlightCelestialOrbit::elevationDegrees(c.sun.direction[2]);
    orbit.moon.elevation=NorthlightCelestialOrbit::elevationDegrees(c.moon.direction[2]);
    orbit.valid=true;
    c.sun.angularRadius=c.sun.nativeAngularRadius;
    c.moon.angularRadius=c.moon.nativeAngularRadius;
    applyRendererPolicy(c,direct);return orbit;
}

struct CodeSignature {uintptr_t address;const unsigned char* bytes;size_t size;};
inline const CodeSignature* signatures(size_t& count) {
    static const unsigned char skyGetter[]={0xb8,0x00,0x8b,0xd3,0x00,0xc3};
    static const unsigned char sunInit[]={0x68,0x90,0x1b,0xa4,0x00,0xb9,0x28,0x8e,0xd3,0x00,0xe8,0x44,0xa7,0x1b,0x00};
    static const unsigned char moonInit[]={0x68,0x7c,0x1b,0xa4,0x00,0xb9,0x48,0x8e,0xd3,0x00,0xd9,0x1d,0x44,0x8e,0xd3,0x00,0xe8,0x27,0xa7,0x1b,0x00};
    static const unsigned char moon2Init[]={0x68,0x68,0x1b,0xa4,0x00,0xd9,0xe8,0xb9,0x68,0x8e,0xd3,0x00,0xd9,0x1d,0x64,0x8e,0xd3,0x00,0xe8,0x04,0xa7,0x1b,0x00};
    static const unsigned char sunPosition[]={0xd9,0x05,0x18,0x8b,0xd3,0x00,0xde,0xc3,0xd9,0xca,0xd9,0x1d,0x28,0x8e,0xd3,0x00};
    static const unsigned char moonPosition[]={0xd8,0x05,0x18,0x8b,0xd3,0x00,0xd9,0x1d,0x48,0x8e,0xd3,0x00};
    static const unsigned char moon2Position[]={0xd8,0x05,0x18,0x8b,0xd3,0x00,0xd9,0x1d,0x68,0x8e,0xd3,0x00};
    static const unsigned char renderTime[]={0xd9,0x05,0x04,0x8b,0xd3,0x00,0x51,0xbe,0x05,0x00,0x00,0x00};
    static const CodeSignature table[]={
        {0x7ecef0,skyGetter,sizeof skyGetter},{0x7f295d,sunInit,sizeof sunInit},
        {0x7f2974,moonInit,sizeof moonInit},{0x7f2995,moon2Init,sizeof moon2Init},
        {0x7ef161,sunPosition,sizeof sunPosition},{0x7ef337,moonPosition,sizeof moonPosition},
        {0x7ef5b6,moon2Position,sizeof moon2Position},{0x7eefee,renderTime,sizeof renderTime}};
    count=sizeof table/sizeof table[0];return table;
}
// RenderSky (0x7f09b0) reads the skybox fields at 0xd38b5c/60/64/70/7c; identical in the stock and HD wow.exe. Kept apart from
// signatures(): a mismatch must not disable the planets, only force the skybox (late disc) path.
inline const CodeSignature* skyboxSignatures(size_t& count) {
    static const unsigned char skybox[]={0xa1,0x5c,0x8b,0xd3,0x00,0x85,0xc0,0xbf,0x01,0x00,0x00,0x00};
    static const unsigned char weight[]={0xd9,0x05,0xf4,0x2f,0x9e,0x00,0xd8,0x1d,0x60,0x8b,0xd3,0x00};
    static const unsigned char blend[]={0xbe,0x70,0x8b,0xd3,0x00,0x8d,0x9b,0x00,0x00,0x00,0x00,0x8b,0x46,0xf4};
    static const unsigned char flags[]={0xd8,0x1e,0xdf,0xe0,0xf6,0xc4,0x05,0x7a,0x08,0x83,0x7e,0x0c,0x00,0x75,0x02,0x33,0xff,0x83,0xc6,0x04,0x81,0xfe,0x7c,0x8b,0xd3,0x00,0x7c,0xc1};
    static const CodeSignature table[]={{0x7f0abe,skybox,sizeof skybox},{0x7f0ae0,weight,sizeof weight},{0x7f0af5,blend,sizeof blend},{0x7f0b23,flags,sizeof flags}};
    count=sizeof table/sizeof table[0];return table;
}
template<class Read> inline bool verifySkyboxCode(Read read) {
    size_t count=0;const auto* table=skyboxSignatures(count);
    unsigned char actual[32];
    for(size_t i=0;i<count;++i)
        if(!read(table[i].address,actual,table[i].size)||
           std::memcmp(actual,table[i].bytes,table[i].size))return false;
    return true;
}
// Unverified skybox offsets: assume a skybox (the late-disc path, which needs no native billboard draw).
inline void assumeSkybox(Context& c,bool verified){if(!verified){c.skyboxCover=1;c.nativePlanets=false;}}
template<class Read> inline bool verifyCode(Read read) {
    size_t count=0;const auto* table=signatures(count);
    unsigned char actual[32];
    for(size_t i=0;i<count;++i)
        if(!read(table[i].address,actual,table[i].size)||
           std::memcmp(actual,table[i].bytes,table[i].size))return false;
    return true;
}
template<class Read> inline bool readSnapshot(Read read,Snapshot& out) {
    // Includes the render clock, sky camera and all three adjacent body records.
    // Two identical copies reject concurrent updates. Call on the render thread (0.3.192 CS: with the command stream on,
    // the replay thread reads this from the game thread's snapshot, which holds the same two copies in order).
    unsigned char a[0x388],b[sizeof a];
    if(!read(0xd38b00,a,sizeof a)||!read(0xd38b00,b,sizeof b)||std::memcmp(a,b,sizeof a))return false;
    Snapshot s;std::memcpy(&s.dayFraction,a+4,4);
    std::memcpy(s.skyCamera,a+0x18,sizeof s.skyCamera);
    std::memcpy(s.bodies,a+0x328,sizeof s.bodies);
    std::memcpy(&s.skybox,a+0x5c,4);std::memcpy(&s.skyboxWeight,a+0x60,4);std::memcpy(s.blendSkybox,a+0x64,12);
    std::memcpy(s.blendWeight,a+0x70,12);std::memcpy(s.blendFlags,a+0x7c,12);out=s;return true;
}
#ifdef _WIN32
inline bool skyboxVerified(){static const bool v=NorthlightWorldContext::supportedClient()&&verifyCode(NorthlightWorldContext::readSelf)&&verifySkyboxCode(NorthlightWorldContext::readSelf);return v;}
inline bool read(const float* camera,const float* direct,Context& out) {
    static const bool supported=NorthlightWorldContext::supportedClient()&&verifyCode(NorthlightWorldContext::readSelf);
    Snapshot snapshot;
    if(!(supported&&readSnapshot(NorthlightWorldContext::readSelf,snapshot)&&decode(snapshot,camera,direct,out)))return false;
    assumeSkybox(out,skyboxVerified());return true;
}
#endif
} // namespace NorthlightCelestial
