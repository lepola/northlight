#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorthlightCelestialHalo {
constexpr unsigned Samples=32;
// Fixed equal-area disc samples, not frame-varying noise. The old five-point
// cross made each twig toggle 20% of the entire halo in one frame.
inline void offset(unsigned i,float& x,float& y){
    const float radius=std::sqrt((float(i)+.5f)/Samples);
    const float angle=float(i)*2.39996322972865332f;
    x=radius*std::cos(angle);y=radius*std::sin(angle);
}
struct History {
    bool valid=false;
    uint32_t tick=0,frame=0,width=0,height=0;
    float camera[3]={},day=0;
    // Partial visibility is FPS independent; the shader cuts full coverage to zero.
    // Return reset/fall/rise for the GPU; no readback or synchronization.
    void advance(uint32_t now,uint32_t nextFrame,uint32_t w,uint32_t h,
                 const float* position,float nextDay,float* control){
        float distance2=0;for(unsigned k=0;k<3;++k){float delta=position[k]-camera[k];distance2+=delta*delta;}
        const uint32_t elapsed=now-tick;
        float dayDelta=std::fabs(nextDay-day);dayDelta=std::min(dayDelta,std::fabs(1-dayDelta));
        bool reset=!valid||nextFrame!=frame+1||w!=width||h!=height||elapsed>250||distance2>128*128||dayDelta>.02f;
        const float dt=float(elapsed)*.001f;
        control[0]=reset?0.f:1.f;
        control[1]=reset?1.f:1-std::exp(-dt/.060f);
        control[2]=reset?1.f:1-std::exp(-dt/.140f);
        control[3]=0;
        valid=true;tick=now;frame=nextFrame;width=w;height=h;day=nextDay;
        for(unsigned k=0;k<3;++k)camera[k]=position[k];
    }
};
}
