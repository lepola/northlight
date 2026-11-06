#pragma once
#include <cstdint>

namespace NorthlightSkyComposition {
enum class Role : unsigned char { Unknown, Background, Foreground };
// The RGB channels retain only the background's contribution. Alpha retains
// transmission. Native foreground colour is already present in the scene.
struct Sample { float background[3]{}, transmission=1; };
inline void over(Sample& sample,Role role,const float* color,float alpha){
    for(unsigned c=0;c<3;++c)sample.background[c]=sample.background[c]*(1-alpha)+(role==Role::Background?color[c]*alpha:0);
    sample.transmission=sample.transmission*(1-alpha)+(role==Role::Background?alpha:0);
}
inline float contribution(float surface,float halo,float background,float transmission){
    return transmission*surface*(1-halo)+halo*(transmission-background);
}
}
