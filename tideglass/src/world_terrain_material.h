#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace water::world
{
// Small alpha maps store successive over operations. Convert each source texel
// before filtering: lerp(filtered masks) differs from filtered layer coverage.
// Big alpha maps already store independent weights and must not be converted.
inline void ResolveTerrainCoverage(uint8_t* weights,unsigned count)
{
    unsigned remaining=255;
    for(unsigned layer=count;layer>0;--layer){
        const unsigned covered=(unsigned(weights[layer-1])*remaining+127)/255;
        weights[layer-1]=static_cast<uint8_t>(covered);remaining-=covered;
    }
}

// MCLY low bits encode eight scroll directions, a three-bit speed, animation
// enable (0x40), and glow (0x80). Alpha presence remains a decoder contract.
inline std::array<float,4> TerrainLayerParameters(uint32_t flags,uint32_t milliseconds)
{
    std::array<float,4> value{0,0,(flags&0x80)?1.f:0.f,0};
    if(flags&0x40){
        const unsigned speed=(flags>>3)&7;
        const double phase=double((uint64_t(milliseconds)*speed)%7000)/7000;
        const double direction=double(flags&7)*.7853981633974483;
        value[0]=float(-std::round(std::sin(direction))*phase);
        value[1]=float(std::round(std::cos(direction))*phase);
    }
    return value;
}
}
