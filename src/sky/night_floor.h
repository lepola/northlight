#pragma once
#include <algorithm>
#include <cmath>

// NightBrightness: lifts the darkest night. The weight ramps from 0 (sun at +4 degrees, dusk
// start) to 1 (sun at -8 degrees) on the same smoothed renderer-owned sun direction as the
// light, so it fades in with sunset and out with sunrise; daytime is exactly 0.
namespace NorthlightNightFloor {
inline constexpr float StartZ=.0697564737f; // sin(+4 degrees)
inline constexpr float EndZ=-.1391731010f; // sin(-8 degrees)
inline constexpr float Reference=1.5f; // reference light level L, in native-ambient luminances
inline constexpr float MinLight=.15f; // the composite clamps its baseline light (oldLight) at .15, so L cannot go below it
inline constexpr float Gain=2.f; // lift at NightBrightness 100 and weight 1 (surfaces at the reference light get +200%)
inline float smooth(float v){v=std::clamp(v,0.f,1.f);return v*v*(3-2*v);}
inline float weight(bool valid,float sunZ){
    if(!valid||!std::isfinite(sunZ)||std::fabs(sunZ)>1)return 0;
    return 1-smooth((sunZ-EndZ)/(StartZ-EndZ));
}
// WorldComposite constants (c19.xy): out[0]=1/(3L) (L = Reference x native ambient luminance; the 3 folds the channel mean into the dot), out[1]=lift A; {0,0} = off.
inline void constants(unsigned percent,float weight,const float ambient[3],float out[2]){
    out[0]=out[1]=0;
    if(percent==0||!std::isfinite(weight)||weight<=0)return;
    const float lum=(ambient[0]+ambient[1]+ambient[2])/3.f;
    if(!std::isfinite(lum)||lum<1e-4f)return;
    out[0]=1.f/(3.f*std::max(Reference*lum,MinLight));
    out[1]=Gain*float(std::min(percent,100u))/100.f*std::min(weight,1.f);
}
// Mirrors the NightFloor line in WorldComposite: color += A * min(L/lum,1) * (color - fogPart), lum = mean(oldLight); 1/x = L/lum.
inline void lift(const float color[3],const float fogPart[3],const float oldLight[3],const float k[2],float out[3]){
    const float x=(oldLight[0]+oldLight[1]+oldLight[2])*k[0];
    const float s=k[1]/std::max(x,1.f);
    for(int i=0;i<3;++i)out[i]=s*(color[i]-fogPart[i])+color[i];
}
}
