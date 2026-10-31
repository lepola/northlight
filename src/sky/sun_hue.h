#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// glow hue H: the game's own zone/time sun glow colour, shared by the sun glare,
// the veil, the fog-pass direct scatter and the horizon lift. CPU policy only: no D3D
// object or game address (the band read and its code proofs live in wmo_context.h).
//
// Source: the client's current light parameters, 18 packed D3DCOLORs at sky block
// +0xd4 (0xd38bd4, copied per frame by 0x7ed910 from the sampler 0x7ebff0). The sampler
// stores LightIntBand band b in slot b, except band 0 (diffuse) -> slot 1, band 1
// (ambient) -> slot 0, band 8 (shadow) -> slot 2 and bands 2..7 -> slots 3..8. So slot
// 10 is band 10 "sunHalo" (Brill (36,63,50), Durotar 20:00 (225,67,1), Orgrimmar 17:00
// (195,75,3)). Slot 9 (band 9 "sun") is the colour of the native sun/moon billboards AND
// of the native sunGlare/moonGlare draw: 0x7f36f6 copies it to +0x0c of the bodies and
// +0x18 of both glare records, and the glare draw (0x9ac5c3) streams record+0x18 as its
// vertex colour. On a stock client band 9 is zone-tinted (Brill (190,240,193), Durotar
// (255,124,25), Orgrimmar (255,170,99)); the HD client zeroes it wherever its skyboxes
// carry their own sun (Brill, Durotar and Orgrimmar included). The native sun
// stays hidden and its colour is kept: the hue is band 9 where it is lit, fading by band-9
// brightness to band 10 where it is black, so HD keeps the band-10 hue and stock the native one.
namespace NorthlightSunHue {
struct GlowHue {
    float sun[3]={1,1,1};     // outer glow hue, normMax (largest channel 1)
    float sunCore[3]={1,1,1}; // hot core at the disc: the hue clipped toward warm white, normMax
    float moon[3]={1,1,1};    // normMax of the regional moon_disc tint (the approved moon colour)
    float strength=0;         // 0..1: how much the sun hue replaces today's colour (0 = today)
    bool valid=false;         // validated native colour or the bounded legacy palette fallback
    bool nativeColors=false; // body RGB and native glare RGB bypass reference-look remapping
    float moonStrength=0;     // 1 when moon holds the regional moon_disc tint (independent of valid), else 0
    float native=0;           // 0..1: share of band 9 (the native sun colour) in sun; the rest is band 10
};

inline constexpr unsigned Slots=18,SlotAmbient=0,SlotDirect=1,SlotSkyAboveHorizon=6,SlotFog=8,SlotSun=9,SlotSunHalo=10;

// Tuned against the reference screenshots: the edge of the glow reads
// (.70,1,.61) Tirisfal, (1,.70,.41) Durotar, (1,.57,.33) Orgrimmar; the core
// (.96,1,.83), (1,.99,.64), (1,.91,.59). band 10 alone is teal at Brill and red at
// Durotar, so it is pulled toward warm white, warmed and then mildly saturated.
inline constexpr float GlowWhite[3]={1,.95f,.75f},WhiteMix=.45f,WarmBlue=.85f,Saturation=.1f;
inline constexpr float CoreWhite[3]={1,.98f,.78f},CoreMix=.8f;
// A band below MinHalo carries no hue (disabled native sun, unlit zones); FullHalo and up is trusted.
inline constexpr float MinHalo=4.f/255,FullHalo=24.f/255;
inline constexpr unsigned HoldMs=2000; // last proven slots kept this long; then today's colours
// Fog passes only (F6): the sun's forward soft cap, the share of the direct scatter
// that takes the hue, and the cool ambient away from the sun.
inline constexpr float SunForwardCap=.95f,MoonForwardCap=.24f,DirectTint=.7f;
inline constexpr float Cool[3]={.85f,.95f,1.12f},CoolMix=.5f;
// Horizon lift colour (WorldComposite c35.yzw) before this change; the hue replaces it by strength.
inline constexpr float Warm[3]={1,.8f,.55f};

inline float smooth(float v){v=std::clamp(v,0.f,1.f);return v*v*(3-2*v);}
inline float lit(const float* c){return smooth((std::max(c[0],std::max(c[1],c[2]))-MinHalo)/(FullHalo-MinHalo));}
inline float luminance(const float* c){return .2126f*c[0]+.7152f*c[1]+.0722f*c[2];}
inline void unpack(std::uint32_t color,float* rgb){rgb[0]=float((color>>16)&255)/255;rgb[1]=float((color>>8)&255)/255;rgb[2]=float(color&255)/255;}
inline bool finite3(const float* c){return c&&std::isfinite(c[0])&&std::isfinite(c[1])&&std::isfinite(c[2]);}
// Largest channel to 1; false (and white) for black or invalid input.
inline bool normMax(const float* in,float* out){
    const float m=finite3(in)?std::max(in[0],std::max(in[1],in[2])):0.f;
    for(unsigned i=0;i<3;++i)out[i]=m>0?std::max(in[i],0.f)/m:1.f;
    return m>0;
}
inline void saturate(const float* in,float amount,float* out){
    const float l=luminance(in);
    for(unsigned i=0;i<3;++i)out[i]=std::max(l+(in[i]-l)*(1+amount),0.f);
}
// Outer glow hue from a light band (0..1 RGB).
inline void sunHue(const float* halo,float* out){
    float h[3];normMax(halo,h);
    for(unsigned i=0;i<3;++i)h[i]+=(GlowWhite[i]-h[i])*WhiteMix;
    h[2]*=WarmBlue;
    float s[3];saturate(h,Saturation,s);normMax(s,out);
}
// Hot core: saturated hues clip toward warm white near the disc (Durotar is not red-cored).
inline void hotCore(const float* hue,float* out){
    float c[3];for(unsigned i=0;i<3;++i)c[i]=hue[i]+(CoreWhite[i]-hue[i])*CoreMix;
    normMax(c,out);
}
// Reference of the shader blend: inner 1 at the disc, 0 at the outer glow.
inline void glowColour(const GlowHue& h,float inner,float* out){
    const float t=smooth(inner);
    for(unsigned i=0;i<3;++i)out[i]=std::clamp(h.sun[i]+(h.sunCore[i]-h.sun[i])*t,0.f,1.f);
}
// slots: the 18 packed light colours, or nullptr when the read failed or is unproven.
inline GlowHue compute(const std::uint32_t* slots,const float* moonTint,const float* nativeSunTint=nullptr){
    GlowHue h;
    h.moonStrength=normMax(moonTint,h.moon)?1.f:0.f;
    // Proven native appearance takes precedence over the reference-look palette.
    // Preserve encoded RGB, including dark/black glare; no white/core hue bias.
    if(finite3(nativeSunTint)&&nativeSunTint[0]>=0&&nativeSunTint[1]>=0&&nativeSunTint[2]>=0&&
       nativeSunTint[0]<=1&&nativeSunTint[1]<=1&&nativeSunTint[2]<=1){
        if(slots)unpack(slots[SlotSun],h.sun);else std::copy(nativeSunTint,nativeSunTint+3,h.sun);
        std::copy(nativeSunTint,nativeSunTint+3,h.sunCore);
        h.native=h.strength=1;h.valid=h.nativeColors=true;return h;
    }
    if(!slots){hotCore(h.sun,h.sunCore);return h;}
    float sun[3],halo[3],fromSun[3],fromHalo[3],mixed[3];
    unpack(slots[SlotSun],sun);unpack(slots[SlotSunHalo],halo);
    sunHue(sun,fromSun);sunHue(halo,fromHalo);
    h.native=lit(sun);
    for(unsigned i=0;i<3;++i)mixed[i]=fromHalo[i]+(fromSun[i]-fromHalo[i])*h.native;
    normMax(mixed,h.sun);hotCore(h.sun,h.sunCore);
    h.strength=std::max(h.native,lit(halo));
    h.valid=true;return h;
}
inline float sunStrength(const GlowHue& h){return h.valid?std::clamp(h.strength,0.f,1.f):0.f;}
// c17 in the fog passes: the direct colour takes the hue at equal luminance, by DirectTint.
inline void fogDirect(const float* direct,const GlowHue& h,float* out){
    const float m=DirectTint*sunStrength(h),l=luminance(direct),lh=luminance(h.sun);
    for(unsigned i=0;i<3;++i){const float tinted=lh>1e-4f?l*h.sun[i]/lh:direct[i];out[i]=direct[i]+(tinted-direct[i])*m;}
}
// c18.xyz in the fog passes: cooler at equal luminance while the sun is up, so the
// broad environment scatter reads cool away from the sun and H toward it.
inline void coolAmbient(const float* ambient,const GlowHue& h,float sunWeight,float* out){
    const float k=CoolMix*sunStrength(h)*(std::isfinite(sunWeight)?std::clamp(sunWeight,0.f,1.f):0.f);
    float f[3];for(unsigned i=0;i<3;++i)f[i]=1+(Cool[i]-1)*k;
    const float l=luminance(f);
    for(unsigned i=0;i<3;++i)out[i]=ambient[i]*f[i]/l;
}
// c35.yzw: the horizon lift colour, today's Warm at strength 0.
inline void horizonLift(const GlowHue& h,float* out){
    const float k=sunStrength(h);
    for(unsigned i=0;i<3;++i)out[i]=Warm[i]+(h.sun[i]-Warm[i])*k;
}
}
