#pragma once
#include <cmath>
#include <cstdint>

// Asset identity and current pose are separate. A later skinned provider needs
// explicit bone state; a character must not silently become a rigid transport.
struct ReflectionObject
{
    uint64_t identity=0;
    uint32_t display=0;
    float transform[16]={}; // complete world pose, row vectors
};
constexpr unsigned kMaxReflectionObjects=16;
// Reflection preparation needs the distant approach, independently of wake range.
constexpr float kReflectionObjectRange=1536.f;
inline bool ValidReflectionObject(const ReflectionObject& object)
{
    if(!object.identity || !object.display || object.display>1000000)return false;
    for(float f:object.transform)if(!std::isfinite(f))return false;
    const auto* m=object.transform;
    if(std::abs(m[3])>.0001f||std::abs(m[7])>.0001f||std::abs(m[11])>.0001f||std::abs(m[15]-1)>.0001f)return false;
    for(unsigned a=0;a<3;++a){
        if(std::abs(m[12+a])>50000)return false;
        const float length=m[a*4]*m[a*4]+m[a*4+1]*m[a*4+1]+m[a*4+2]*m[a*4+2];
        if(length<.000001f||length>10000)return false;
    }
    const double determinant=double(m[0])*(double(m[5])*m[10]-double(m[6])*m[9])-
        double(m[1])*(double(m[4])*m[10]-double(m[6])*m[8])+double(m[2])*(double(m[4])*m[9]-double(m[5])*m[8]);
    return determinant>1e-9; // mirrored/degenerate poses need a separate winding policy
}
