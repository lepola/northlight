#pragma once
#include <array>
#include <cmath>

namespace water::world
{
// One declared diagnostic camera region. This is partial scene admission, not
// global reflection visibility or a streaming/LOD policy.
struct OwnedRiverRegion
{
    inline static constexpr std::array<float,3> minimum={-9480,-220,55},maximum={-9215,-70,160};
    inline static constexpr std::array<float,3> lakeMinimum={-9560,-460,25},lakeMaximum={-9215,-70,160};
    static bool Camera(const float* p,bool lake=false)
    {
        if(!p)return false;
        for(unsigned axis=0;axis<3;++axis) {
            const float low=lake?lakeMinimum[axis]:(axis==2?25:minimum[axis]);
            if(!std::isfinite(p[axis])||p[axis]<low||p[axis]>(lake?lakeMaximum[axis]:maximum[axis]))return false;
        }
        return true;
    }
    static bool Building(const std::array<float,3>& low,const std::array<float,3>& high,bool lake=false)
    {
        // Conservative source AABB overlap, including roots whose origins are
        // outside the region. Far city/dungeon coverage remains explicitly partial.
        constexpr float margin=128;
        for(unsigned axis=0;axis<2;++axis)
            if(std::isfinite(low[axis])&&std::isfinite(high[axis])&&
               (high[axis]<(lake?lakeMinimum[axis]:minimum[axis])-margin||low[axis]>(lake?lakeMaximum[axis]:maximum[axis])+margin))return false;
        return true;
    }
};
}
