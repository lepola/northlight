#pragma once
#include <cmath>

namespace NorthlightFogAmbient {
// The specialization is valid only for a proven zero direct-scattering term.
// Unknown constants retain the complete shader and its original behavior.
inline bool eligible(const float* rgb,float gain){
    return rgb&&std::isfinite(gain)&&std::isfinite(rgb[0])&&std::isfinite(rgb[1])&&std::isfinite(rgb[2])&&
           (gain<=0||(rgb[0]<=0&&rgb[1]<=0&&rgb[2]<=0));
}
}
