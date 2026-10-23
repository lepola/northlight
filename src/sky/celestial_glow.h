#pragma once
// Reference-look glow: hue, broad solar glare, lunar skirt, wrap ring and
// the veil's gating. Portable CPU policy (no D3D object or game address): host
// constants for CelestialDiscPS/CelestialVeilPS and exact mirrors of their
// profile maths for the tests. No user-facing key; these are the defaults.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "celestial_halo_visibility.h"

namespace NorthlightCelestialGlow {
// Terrain-mask extent (prepare gate, mask matrix, c46.y) and glare support
// (scissor, c12.y). terrainVisibility() returns 1 outside the mask, and loaded
// terrain past the game's far clip is not in the depth buffer at all. A mask
// narrower than the glare (10R/2.5R before 0.3.217) let the glow wash distant
// mountains outside it and not inside: a sharp square edge, seen as a diamond
// when the world-aligned mask basis lies diagonally on screen (a radial fade
// of that mask left a dark blot round the body instead). The mask therefore
// spans the whole glare support; it holds only terrain past the far clip and
// is redrawn when the eye has moved enough to shift it (celestial_terrain.h).
constexpr float SunGlareSupport=20.f,MoonGlareSupport=6.f;
constexpr float SunMaskRadius=SunGlareSupport,MoonMaskRadius=MoonGlareSupport;
// Glare profile (0.3.164; user: 0.3.163 "like a football with sharp edges").
// The shader outputs 1-exp(-x*colour) through a screen blend, so nothing clips
// to a plateau: x = core weight*2^(-rate r²) (the small hot core, ~1-1.5R)
// + tail weight*T(r) (the long soft tinted tail to the 20R support). The core
// colour follows the same core falloff. Toward a low sun (15 -> 3 deg) only
// the tail grows (a broader wash at sunset), still smooth. The lunar profile
// carries its own .35 peak: strength 1 is the agreed M1 skirt
// .35*2^(-1.11(r-1)) (.27 at 1.33R, .17 at 1.9R), now also through the shoulder.
constexpr float SunCoreWeight=2.4f,SunCoreRate=1.1f,SunTailWeight=.9f,SunTailWeightLow=1.8f,MoonGlareStrength=1.f;
constexpr float LowSunStart=.2588f,LowSunEnd=.0523f; // sin 15 deg, sin 3 deg
// Veil over geometry relative to the sky glare, and ring wrap.
constexpr float VeilStrength=.75f,Wrap=.6f;
// Ring taps (disc radii) for the wrap visibility; inside the 20R sun mask.
constexpr float RingInner=2.f,RingOuter=4.f;

// Per-frame glow colours in encoded 0..1 (from the renderer's GlowHue).
// strength 0 = the profile disc tint, today's colour.
struct Hue {
    float sun[3]={1,.96f,.88f},sunCore[3]={1,.96f,.88f},moon[3]={.94f,.97f,1};
    float strength=0,moonStrength=0; // sun and moon hue mix
};
inline float clamp01(float x){return std::isfinite(x)?std::clamp(x,0.f,1.f):0.f;}
// c47 (hue, mix) and c48 (hot core, wrap) for one body.
inline void constants(const Hue& hue,unsigned body,float wrap,float* c47,float* c48){
    for(unsigned k=0;k<3;++k){
        c47[k]=clamp01(body?hue.moon[k]:hue.sun[k]);
        c48[k]=clamp01(body?hue.moon[k]:hue.sunCore[k]);
    }
    c47[3]=clamp01(body?hue.moonStrength:hue.strength);c48[3]=body?0.f:clamp01(wrap);
}

// 0 with the sun above 15 deg, 1 below 3 deg (sine of the elevation).
inline float lowSun(float sunElevationSine){
    if(!std::isfinite(sunElevationSine))return 0;
    const float t=std::clamp((LowSunStart-sunElevationSine)/(LowSunStart-LowSunEnd),0.f,1.f);return t*t*(3-2*t);
}
inline float sunTailWeight(float sunElevationSine){return SunTailWeight+(SunTailWeightLow-SunTailWeight)*lowSun(sunElevationSine);}
// GLARE (and veil) colour constants: the hue mix is folded in on the CPU
// (hue' = lerp(tint,hue,mix), core' = lerp(tint,core,mix)); the shader then
// computes saturate(lerp(hue', core', w(r))), equal to lerp(tint, lerp(hue,
// core, w), mix). c47.w = the tail weight (sun), c48.w = the ring wrap.
// Disc draws keep constants() (core tint by mix).
inline void glareConstants(const Hue& hue,unsigned body,const float* tint,float tail,float wrap,float* c47,float* c48){
    const float m=clamp01(body?hue.moonStrength:hue.strength);
    for(unsigned k=0;k<3;++k){
        const float t=clamp01(tint[k]);
        c47[k]=t+(clamp01(body?hue.moon[k]:hue.sun[k])-t)*m;c48[k]=t+(clamp01(body?hue.moon[k]:hue.sunCore[k])-t)*m;
    }
    c47[3]=body?0.f:std::max(tail,0.f);c48[3]=body?0.f:clamp01(wrap);
}
// Mirrors of the shader (float maths, same constants).
inline float smoothstep(float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
inline float sunTail(float radius){const float r2=radius*radius;return .25f*std::exp2(-.12f*r2)+.35f*std::exp2(-.025f*r2)+.40f*std::exp2(-.008f*r2);}
inline float sunProfile(float radius,float tail=SunTailWeight,float core=SunCoreWeight,float support=SunGlareSupport){
    return (core*std::exp2(-SunCoreRate*radius*radius)+tail*sunTail(radius))*(1-smoothstep(.35f*support,support,radius));
}
// With occlusion: the core uses the disc taps' visibility only; the ring
// (x wrap) carries only the core-free tail shifted out by 3R,
// sunTail(sqrt(r^2+9)). Both parts are non-increasing in r, so the profile is
// monotonic for every (disc, ring) pair: no dark ring around a barely hidden sun.
inline float sunGlare(float radius,float disc,float ring,float wrap,float tail=SunTailWeight,float core=SunCoreWeight,float support=SunGlareSupport){
    const float d=clamp01(disc),w=clamp01(wrap)*clamp01(ring);
    const float tailed=std::max(d*sunTail(radius),w*sunTail(std::sqrt(radius*radius+9)));
    return (core*std::exp2(-SunCoreRate*radius*radius)*d+tail*tailed)*(1-smoothstep(.35f*support,support,radius));
}
// Soft shoulder of the screen blend: final = 1-(1-dst)*exp(-x*colour).
inline float shoulder(float destination,float x,float colour){return 1-(1-destination)*std::exp2(-1.442695f*x*colour);}
inline float moonProfile(float radius,float support=MoonGlareSupport){
    return .35f*std::exp2(-1.11f*std::max(radius-1,0.f))*(1-smoothstep(.35f*support,support,radius));
}
// Hot-core weight: smoothstep(inner) with inner = 2^(-SunCoreRate r²) (the core
// term's own falloff), 1 at the disc centre (.45 at 1R, .09 at 1.5R, .007 at 2R).
// Colour = lerp(tint, lerp(hue, core, weight), strength): the shader's
// glowColor() (mix folded on the CPU) and NorthlightSunHue::glowColour().
inline float coreWeight(float radius){return smoothstep(0,1,std::exp2(-SunCoreRate*radius*radius));}
inline void glowColour(const float* tint,const float* hue,const float* core,float strength,float radius,float* out){
    const float w=coreWeight(radius),m=clamp01(strength);
    for(unsigned k=0;k<3;++k){const float t=clamp01(tint[k]),g=clamp01(hue[k]+(core[k]-hue[k])*w);out[k]=t+(g-t)*m;}
}
inline float sunDiscAlpha(float radius){const float a=std::clamp((1-radius)/.56f,0.f,1.f);return a*a*(3-2*a);}
// The TAIL's visibility beyond ~3R (the core uses the disc only; see sunGlare).
inline float occlusion(float disc,float ring,float wrap){return std::max(clamp01(disc),clamp01(wrap)*clamp01(ring));}

// Keep the diffuse halo when a small foreground silhouette hides the disc.
// Fade as the disc crosses the true horizon, from its top to its bottom limb.
// The disc/core and water retain their own strict per-pixel occlusion.
inline float ringWrap(float elevationSine,float sinRadius){
    if(!std::isfinite(elevationSine)||!(sinRadius>0))return 0;
    return Wrap*smoothstep(-sinRadius,sinRadius,elevationSine);
}
// Equal-area annulus RingInner..RingOuter, golden-angle order like the disc taps.
inline void ringOffset(unsigned i,float& x,float& y){
    const float a=RingInner*RingInner,b=RingOuter*RingOuter;
    const float radius=std::sqrt(a+(b-a)*(float(i)+.5f)/NorthlightCelestialHalo::Samples);
    const float angle=float(i)*2.39996322972865332f;
    x=radius*std::cos(angle);y=radius*std::sin(angle);
}
// CelestialHaloVisibilityPS averages 32 geometry/terrain/horizon taps and an
// off-screen tap (xy = -1) counts as hidden. Refill the slots from the
// on-screen taps, spread evenly, so its fixed 1/32 mean is the mean over the
// taps on screen: the value no longer dims as the source nears the screen edge.
// Returns the on-screen fraction; with none on screen all slots stay invalid.
inline float normalizeOnScreen(float (*taps)[4],unsigned count){
    unsigned valid[64],n=0;count=std::min(count,64u);
    for(unsigned i=0;i<count;++i)if(taps[i][0]>=0)valid[n++]=i;
    if(!n||n==count)return count?float(n)/float(count):0.f;
    float copy[64][4];
    for(unsigned i=0;i<count;++i)for(unsigned k=0;k<4;++k)copy[i][k]=taps[i][k];
    for(unsigned s=0;s<count;++s){const unsigned from=valid[s*n/count];for(unsigned k=0;k<4;++k)taps[s][k]=copy[from][k];}
    return float(n)/float(count);
}
// Screen-edge fade of the glare and veil: by how far the disc CENTRE lies
// outside the screen, in disc radii (continuous, not the 1/32 tap count).
// Full until the disc has left (1R), zero at 6R; the glow holds the last
// on-screen visibility meanwhile, so a 20R glow cannot vanish in a few pixels.
constexpr float EdgeFadeStart=1.f,EdgeFadeEnd=6.f;
inline float edgeFade(float outsideRadii){return 1-smoothstep(EdgeFadeStart,EdgeFadeEnd,outsideRadii);}
// Distance of the projected centre (x,y pixels) outside a width x height
// screen, in projected disc radii; 0 on screen, a large value behind the camera.
inline float outsideRadii(bool inFront,float x,float y,float radiusPixels,float width,float height){
    if(!inFront||!std::isfinite(x)||!std::isfinite(y)||!(radiusPixels>0))return 1e9f;
    const float dx=std::max(std::max(-x,x-width),0.f),dy=std::max(std::max(-y,y-height),0.f);
    return std::sqrt(dx*dx+dy*dy)/radiusPixels;
}
// The fade filtered like the halo visibility (fall 60 ms, rise 140 ms, FPS
// independent; a skipped frame or stale state starts at the target).
struct Fade {
    bool valid=false;std::uint32_t tick=0,frame=0;float value=0;
    float update(std::uint32_t now,std::uint32_t nextFrame,float target){
        const std::uint32_t elapsed=now-tick;target=clamp01(target);
        if(!valid||nextFrame!=frame+1||elapsed>250)value=target;
        else value+=(target-value)*(1-std::exp(-float(elapsed)*.001f/(target<value?.060f:.140f)));
        valid=true;tick=now;frame=nextFrame;return value;
    }
    // No visibility is known (never measured on screen): start the fade-in from 0.
    void zero(std::uint32_t now,std::uint32_t nextFrame){valid=true;tick=now;frame=nextFrame;value=0;}
};

// Horizon haze amount (WorldComposite horizonHaze(), sky pixel, range 1) at
// the sun's elevation. The glare is drawn before the composite and hazed per
// pixel there; the veil is drawn after it, so it is scaled by 1 - this.
// opticalDepth = c34.w (tau*log2 e), band = c57.w (log2 e / sin band).
inline float hazeAtSun(float opticalDepth,float band,float sunElevationSine){
    if(!std::isfinite(opticalDepth)||!(opticalDepth>0)||!std::isfinite(band)||!std::isfinite(sunElevationSine))return 0;
    return 1-std::exp2(-opticalDepth*std::exp2(-std::max(sunElevationSine,0.f)*band));
}
// Veil scale of the glare weights (CelestialVeilPS c12.z, c47.w); 0 = skip the
// draw. skyTransmittance: the sky glare is dimmed by the composite's fog
// transmittance, the veil drawn after it is not; scaling by it keeps geometry
// from outshining the sky beside it.
inline float veilStrength(float opacity,float edge,float haze,float skyTransmittance=1){
    const float v=VeilStrength*clamp01(opacity)*clamp01(edge)*(1-clamp01(haze))*clamp01(skyTransmittance);
    return v>=1.f/512?v:0.f;
}
} // namespace NorthlightCelestialGlow
