#pragma once
// Frozen reduction from 748a18bda64182b1e26ff146e25bf7d9b6250e5f, MIT (repository LICENSE).
#include "regional_fog.h"
namespace FrozenFogBounds {
using namespace NorthlightRegionalFog;
inline std::array<float,3> globalHeightBounds(const Field& field){
    std::array<float,3> out{};
    for(const auto& t:field.texels){
        if(!texelFinite(t))return {};
        if(t.height<=0)continue;
        out[0]=std::min(out[0],t.ground);out[1]=std::max(out[1],t.ground);out[2]=std::max(out[2],t.height);
    }
    return out;
}
}
