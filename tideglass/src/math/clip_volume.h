#pragma once
#include <array>
#include <cmath>

namespace water::math
{
// Six inward-facing clip planes, prepared once per view. Testing the box's
// farthest corner against each plane is equivalent to projecting all eight
// corners, without repeating the camera transform for every terrain cell.
// Coordinates remain camera-relative to preserve precision in large maps.
class ClipVolume
{
public:
    ClipVolume(const float* camera, const double (&clip)[4][4],
               const std::array<double,4>& extent)
    {
        for (unsigned axis=0; axis<3; ++axis) m_camera[axis]=camera[axis];
        for (unsigned row=0; row<4; ++row) {
            m_planes[0][row]=clip[row][0]-extent[0]*clip[row][3];
            m_planes[1][row]=extent[2]*clip[row][3]-clip[row][0];
            m_planes[2][row]=clip[row][1]-extent[1]*clip[row][3];
            m_planes[3][row]=extent[3]*clip[row][3]-clip[row][1];
            m_planes[4][row]=clip[row][2];
            m_planes[5][row]=clip[row][3]-clip[row][2];
        }
        for (double value:m_camera) m_valid&=std::isfinite(value);
        for (const auto& plane:m_planes)
            for (double value:plane) m_valid&=std::isfinite(value);
    }

    bool Outside(const std::array<float,6>& bounds) const
    {
        if (!m_valid) return false;
        for (float value:bounds) if (!std::isfinite(value)) return false;
        for (unsigned axis=0; axis<3; ++axis)
            if (bounds[axis]>bounds[axis+3]) return false;
        for (const auto& plane:m_planes) {
            double farthest=plane[3];
            for (unsigned axis=0; axis<3; ++axis)
                farthest+=(double(bounds[axis+(plane[axis]>=0?3:0)])-m_camera[axis])*plane[axis];
            // Retain contact at a clip plane, including floating-point roundoff.
            if (farthest < -1e-9) return true;
        }
        return false;
    }

private:
    std::array<double,3> m_camera{};
    std::array<std::array<double,4>,6> m_planes{};
    bool m_valid=true;
};
}
