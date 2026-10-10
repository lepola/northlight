#pragma once
#include <algorithm>
#include <cmath>

// NightBrightness: how much of the art layer's darker night ambient is given back, as added light
// (shadows and lamps keep their contrast). The weight ramps from 0 (sun at
// +4 degrees, dusk start) to 1 (sun at -8 degrees) on the same smoothed renderer-owned sun
// direction as the light, so it fades in with sunset and out with sunrise; daytime is exactly 0.
namespace NorthlightNightFloor {
inline constexpr float StartZ=.0697564737f; // sin(+4 degrees)
inline constexpr float EndZ=-.1391731010f; // sin(-8 degrees)
inline float smooth(float v){v=std::clamp(v,0.f,1.f);return v*v*(3-2*v);}
inline float weight(bool valid,float sunZ){
    if(!valid||!std::isfinite(sunZ)||std::fabs(sunZ)>1)return 0;
    return 1-smooth((sunZ-EndZ)/(StartZ-EndZ));
}
// The art layer's night ambient (build_lighting.py transform_color, channel 1 at night: x .82 x (.85,.97,1.10)) given
// back at full NightBrightness: 1/factor-1 per channel. A literal in WorldComposite; test_night_floor_wiring checks both.
inline constexpr float ArtLayerNightAmbient[3]={.435f,.257f,.109f};
// WorldComposite constant (c19.x): 0 = off, 1 = the night ambient back at the stock game's level.
inline float blend(unsigned percent,float weight){
    if(percent==0||!std::isfinite(weight)||weight<=0)return 0;
    return float(std::min(percent,100u))/100.f*std::min(weight,1.f);
}
}
