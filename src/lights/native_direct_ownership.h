#pragma once
#include "equipment_lights.h"

// Presence belongs to the current native scene, not to a particular receiver's
// four slots. Ambiguous/coincident sources keep the original draw.
namespace NorthlightNativeDirect {
inline bool close(float a,float b,float tolerance){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=tolerance*std::max({1.f,std::fabs(a),std::fabs(b)});}
inline bool same(const NorthlightEquipmentLights::Native& a,const NorthlightNativeLights::Point& b){
    for(unsigned k=0;k<3;++k)if(std::fabs(a.position[k]-b.position[k])>.005f||!close(a.color[k],b.color[k],.0001f)||!close(a.attenuation[k],b.attenuation[k],.0001f))return false;
    return true;
}
inline const NorthlightNativeLights::Point* source(const NorthlightLocalLights::Light& light,const NorthlightNativeLights::Snapshot& scene){
    if(light.kind!=2||(light.flags&NorthlightLocalLights::Flash)||!scene.valid||scene.count>NorthlightNativeLights::Capacity)return nullptr;
    const NorthlightNativeLights::Point* match=nullptr;
    for(unsigned i=0;i<scene.count;++i){const auto& point=scene.lights[i];
        NorthlightLocalLights::Light native=light;std::copy_n(point.position,3,native.position);std::copy_n(point.color,3,native.diffuse);
        native=NorthlightLocalLights::nativeTransport(native,point.attenuation);
        bool equal=true;for(unsigned k=0;k<3;++k)equal&=std::fabs(light.position[k]-native.position[k])<=.005f&&close(light.diffuse[k],native.diffuse[k],.0001f);
        equal&=close(light.attenuationStart,native.attenuationStart,.0001f)&&close(light.attenuationEnd,native.attenuationEnd,.0001f);
        if(equal){if(match)return nullptr;match=&point;}
    }
    return match;
}
inline const NorthlightNativeLights::Point* slot(const NorthlightEquipmentLights::Native& value,const NorthlightNativeLights::Snapshot& scene){
    const NorthlightNativeLights::Point* match=nullptr;if(!scene.valid||scene.count>NorthlightNativeLights::Capacity)return nullptr;
    for(unsigned i=0;i<scene.count;++i)if(same(value,scene.lights[i])){if(match)return nullptr;match=&scene.lights[i];}
    return match;
}
} // namespace NorthlightNativeDirect
