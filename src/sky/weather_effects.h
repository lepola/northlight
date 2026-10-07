#pragma once
// 0.3.198 (rain): what detected rain or snow does to Northlight's OWN effects (the game's storm look is the art layer's
// Light.dbc). Pure: the WeatherState in, a handful of scalars and gains out. Every gain is exactly 1 and every addend exactly
// 0 when the factors are 0 (no weather, Weather=0, RainFog=0), so a dry frame reproduces every constant and every shader
// result of the build without this header. The renderer, the horizon haze, the disc renderer and the shaders only multiply
// by the gains / add the addends; none of them branches on a weather kind.
#include "weather_state.h"
#include <algorithm>
#include <cmath>

namespace NorthlightWeatherEffects {
// Coefficients, all per unit of f (the fog factor below) unless named otherwise.
constexpr float kSnowFog=0.6f;        /* snow acts on the fog-like effects at 60 percent of rain, and never wets */
constexpr float kHazeTau=0.75f;       /* horizon haze optical depth x (1 + kHazeTau f); moderate, the DBC storm bands already fog the game */
constexpr float kAirExtinction=0.0012f; /* extra air extinction beside the shader's literal .0017 (WorldFog c59.w, mirrored in sigmaAt) */
constexpr float kShafts=0.75f;        /* direct volume gain (c21.y) x (1 - kShafts f) */
constexpr float kDisc=0.85f;          /* sun and moon disc opacity, glare weights and veil x (1 - kDisc f) */
constexpr float kShadowSoften=0.5f;   /* direct shadowing weakened by kShadowSoften f (c59.z; 0 = untouched) */
constexpr float kLampFog=0.f;         /* lamp glow in fog (c58.z, c58.w) x (1 + kLampFog f); 0.3.199: 0 (was .5): the denser rain air already brightens the glow */
constexpr float kAmbientLift=0.25f;   /* AmbientLight.w (GI sky ambient boost) += kAmbientLift r, rain only */
constexpr float kIntensityFloor=0.35f; /* light rain still shows: the effect amount is blend x (floor + (1-floor) x intensity) */
constexpr float kMaxShadowSoften=0.95f; /* RainFog=2 never removes sun shadows altogether */

struct Frame {
    float fog=0;  /* f: rain r or snow s, blend x (floor + (1-floor) intensity) x RainFog (x kSnowFog for snow) */
    float rain=0; /* r: rain only, the same product (0 for snow) */
    float wet=0;  /* w: the tracker's slow wetness x RainWetness (not snow) */
    float hazeTauScale()const{return 1+kHazeTau*fog;}
    float airExtinction()const{return kAirExtinction*fog;}
    float shaftGain()const{return std::max(0.f,1-kShafts*fog);}
    float discGain()const{return std::max(0.f,1-kDisc*fog);}
    float shadowSoften()const{return std::min(kMaxShadowSoften,kShadowSoften*fog);}
    float lampFogGain()const{return 1+kLampFog*fog;}
    float ambientLift()const{return kAmbientLift*rain;}
    bool any()const{return fog>0||wet>0;}
};
// weather: Weather 0/1; rainFog, rainWetness: RainFog / RainWetness 0..2. Non-finite or negative inputs count as 0.
inline Frame derive(const NorthlightWeather::State& s,unsigned weather,unsigned rainFog,unsigned rainWetness){
    using NorthlightWeather::Kind;
    Frame f;
    if(!weather)return f;
    const float level=std::isfinite(s.blend)&&std::isfinite(s.intensity)?std::clamp(s.blend,0.f,1.f)*(kIntensityFloor+(1-kIntensityFloor)*std::clamp(s.intensity,0.f,1.f)):0.f; /* exactly 0 when blend is 0 */
    if(s.kind==Kind::Rain){f.fog=f.rain=level*float(std::min(rainFog,2u));}
    else if(s.kind==Kind::Snow)f.fog=kSnowFog*level*float(std::min(rainFog,2u));
    // wetness is the tracker's own slow scalar (rises while it rains, dries over ~90 s after it, so it outlives kind None);
    // snow never wets, and a dry frame is exactly 0
    if(s.kind!=Kind::Snow&&std::isfinite(s.wetness))f.wet=std::clamp(s.wetness,0.f,1.f)*float(std::min(rainWetness,2u));
    return f;
}
}
