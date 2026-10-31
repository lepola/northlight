#pragma once
#include "world_local_lights.h"
#include "world_gi.h"
#include <cmath>
#include <cstddef>

// A bounded FGS2 fixture tag is resolved to its exact FGL source identity on
// FGS3 instance expansion. Incomplete or ambiguous enclosures fail closed.
namespace NorthlightWmoLampShadow {
inline bool matches(const NorthlightLocalLights::Light& light,uint64_t sourceId,unsigned triangles,
                    NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    if(!(light.flags&NorthlightLocalLights::GlassEmitter)||(light.kind!=1&&light.kind!=3)||
       !sourceId||sourceId!=light.sourceId||triangles!=8||
       !(light.attenuationEnd>0.f)||!std::isfinite(light.attenuationEnd)||
       light.attenuationStart<0.f||!std::isfinite(light.attenuationStart))return false;
    const float radius=light.kind==1?light.attenuationEnd:light.attenuationStart;
    if(!(radius>0.f))return false;
    const float lo[3]={low.x,low.y,low.z},hi[3]={high.x,high.y,high.z};float distance=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i]||hi[i]-lo[i]>2.f*radius||
           !std::isfinite(light.position[i]))return false;
        if(light.kind==1&&(light.position[i]<lo[i]-.02f||light.position[i]>hi[i]+.02f))return false;
        const float delta=(lo[i]+hi[i])*.5f-light.position[i];distance+=delta*delta;
    }
    const float tolerance=.02f+.12f*radius;
    return light.kind==1||distance<=tolerance*tolerance;
}
template<class Batches>
inline size_t unique(const NorthlightLocalLights::Light& light,const Batches& batches){
    size_t found=size_t(-1);
    if(!(light.flags&NorthlightLocalLights::GlassEmitter))return found;
    for(size_t i=0;i<batches.size();++i){const auto& b=batches[i];
        if(!b.terrain&&matches(light,b.lampSourceId,b.count,b.boundsLow,b.boundsHigh)){
            if(found!=size_t(-1))return size_t(-1);found=i;
        }}
    return found;
}
}
