#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace NorthlightNativeLights {
inline constexpr unsigned Capacity=128;
inline constexpr std::uint32_t RootAnchor=1,WorldFixture=2;
struct Point {
    std::uint64_t id=0;
    float position[3]{},color[3]{},attenuation[3]{};
    std::uint32_t reserved=0;
    float anchor[3]{};std::uint32_t flags=0;
};
static_assert(sizeof(Point)==64,"native light value layout");
// Owned values only: the replay thread never follows client model pointers.
struct Snapshot {
    std::uint32_t scene=0,time=0,count=0,valid=0;
    char map[64]{};
    std::array<Point,Capacity> lights{};
    void clear(){scene=time=count=valid=0;map[0]=0;}
};
inline bool wellFormed(const Snapshot& s){
    if(s.valid>1||s.count>Capacity||!std::memchr(s.map,0,sizeof s.map))return false;
    if(!s.valid)return s.count==0;
    if(!s.scene)return false;
    for(unsigned i=0;i<s.count;++i){const auto& p=s.lights[i];
        if(!p.id||(p.flags&~(RootAnchor|WorldFixture))||((p.flags&WorldFixture)?(!(p.flags&RootAnchor)||(p.id>>52)!=0xfea||!p.reserved||p.reserved>4096):p.reserved!=0))return false;
        for(float value:p.anchor)if(!std::isfinite(value)||std::fabs(value)>=100000)return false;
        for(unsigned k=0;k<3;++k)
            if(!std::isfinite(p.position[k])||std::fabs(p.position[k])>=100000||
               !std::isfinite(p.color[k])||p.color[k]<0||p.color[k]>10000||
               !std::isfinite(p.attenuation[k])||p.attenuation[k]<0||p.attenuation[k]>10000)return false;
    }
    return true;
}
}
