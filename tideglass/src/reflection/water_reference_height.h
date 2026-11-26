#pragma once
#include "world/water_surface.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace reflection {
// Setup reference only: detailed reflected placement still uses the existing
// world height field. Never average distinct lakes or select by view direction.
inline bool WorldWaterReference(const std::vector<WaterSurface>& surfaces,const float* camera,float& height) {
    if(!camera || !std::isfinite(camera[0]) || !std::isfinite(camera[1]))return false;
    const WaterSurface* best=nullptr;double distance=0,selected=0;
    for(const auto& surface:surfaces){
        bool valid=std::isfinite(surface.normal[2]) && surface.normal[2]>=.25;
        for(unsigned c=0;c<3;++c)valid=valid && std::isfinite(surface.anchor[c]) &&
            std::isfinite(surface.normal[c]) && std::isfinite(surface.bounds[c]) &&
            std::isfinite(surface.bounds[c+3]) && surface.bounds[c]<=surface.bounds[c+3];
        if(!valid)continue;
        const double x=std::clamp(double(camera[0]),double(surface.bounds[0]),double(surface.bounds[3]));
        const double y=std::clamp(double(camera[1]),double(surface.bounds[1]),double(surface.bounds[4]));
        const double dx=x-camera[0],dy=y-camera[1],d=dx*dx+dy*dy;
        const double z=surface.anchor[2]-(surface.normal[0]*(x-surface.anchor[0])+
            surface.normal[1]*(y-surface.anchor[1]))/surface.normal[2];
        if(!std::isfinite(z))continue;
        if(!best || d<distance || (d==distance && (surface.key<best->key ||
           (surface.key==best->key && surface.anchor<best->anchor)))){
            best=&surface;distance=d;selected=std::clamp(z,double(surface.bounds[2]),double(surface.bounds[5]));
        }
    }
    if(!best)return false;height=float(selected);return true;
}
}
