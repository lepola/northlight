#pragma once
#include "owned_world_streaming.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace water
{
// Distances to the four valid edges, measured from the camera. Published
// scenery may grant more coverage in one direction while restricting another.
class ReflectionCoverage
{
public:
    void Update(const float* camera,const world::OwnedWorldRegion& region,float reach,uint32_t tick,float maximumRadius=1.e9f)
    {
        const float overlap=std::min(world::OwnedWorldRegion::Hold,reach*.5f);
        const float detailRadius=reach-overlap;
        const float detailWidth=std::min(96.f,detailRadius*.25f);
        // Carry low-detail bank colour into half the streaming overlap.
        // Each edge is still clipped against the actual resident rectangle.
        const float radius=std::min(detailRadius+.5f*overlap,maximumRadius);
        const float targetWidth=std::min(detailWidth+.5f*overlap,maximumRadius*.5f);
        const float dt=std::min(uint32_t(tick-m_tick),uint32_t(50))*.001f;
        // Preserve the handoff speed as the colour band widens. Bound each
        // frame's advance even when loading interrupts the render clock.
        const float advance=2.f*detailWidth*dt;
        for(unsigned axis=0;axis<2;++axis){
            const float offset=camera[axis]-region.Centre(axis);
            const float low=std::min(radius,reach+offset);
            const float high=std::min(radius,reach-offset);
            m_extent[axis]=m_valid?std::min(low,m_extent[axis]+advance):low;
            m_extent[axis+2]=m_valid?std::min(high,m_extent[axis+2]+advance):high;
            edges[axis]=camera[axis]-m_extent[axis];
            edges[axis+2]=camera[axis]+m_extent[axis+2];
        }
        width=m_valid?std::clamp(targetWidth,width-advance,width+advance):targetWidth;
        m_tick=tick;
        m_valid=true;
    }
    std::array<float,4> edges{};
    float width=0;
private:
    std::array<float,4> m_extent{};
    uint32_t m_tick=0;
    bool m_valid=false;
};
}
