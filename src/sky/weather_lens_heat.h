#pragma once
#include "world_local_lights.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace NorthlightWeatherLensHeat {
// Only classified flames supply heat. Bright windows, blue crystals and spell
// highlights cannot melt snow merely because their pixels are bright.
template<class Lights,class Blocked>
float sample(const Lights& lights,const float* camera,const float* player,Blocked blocked){
    struct Candidate {float strength=0;const NorthlightLocalLights::Light* light=nullptr;const float* origin=nullptr;};
    std::array<Candidate,2> best{};
    for(const auto& light:lights){
        if(!(light.flags&NorthlightLocalLights::FirePulse)||!NorthlightLocalLights::valid(light))continue;
        const float power=std::max({light.diffuse[0],light.diffuse[1],light.diffuse[2]});
        const float reach=std::clamp(4.f*std::sqrt(power),1.5f,8.f);
        for(const float* origin:{camera,player})if(origin){
            float d2=0;for(unsigned k=0;k<3;++k){const float d=light.position[k]-origin[k];d2+=d*d;}
            const float falloff=std::max(0.f,1-d2/(reach*reach));
            const float strength=falloff*falloff*std::min(1.f,power);
            if(strength>best[0].strength){best[1]=best[0];best[0]={strength,&light,origin};}
            else if(strength>best[1].strength)best[1]={strength,&light,origin};
        }
    }
    float heat=0;
    for(const auto& c:best)if(c.light&&c.strength>heat&&!blocked(c.origin,c.light->position))heat=c.strength;
    return heat; // At most two short visibility rays, independent of map size.
}
}
