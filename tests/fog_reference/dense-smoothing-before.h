#pragma once
#include <algorithm>
#include <cmath>
namespace NorthlightFogClouds {
constexpr float kDenseSeconds=3.f;
inline float smoothDense(float current,float target,float dt){
    if(!std::isfinite(current))current=0;if(!std::isfinite(target))target=0;
    const float d=std::isfinite(dt)?std::clamp(dt,0.f,.1f):0.f;return current+(target-current)*(1-std::exp(-d/kDenseSeconds));}
}
