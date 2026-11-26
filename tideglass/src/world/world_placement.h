#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace water::world
{
// Apply an attachment's local row-vector pose before the containing world pose.
inline bool ComposePlacement(const float* local,const float* parent,std::array<float,16>& output)
{
    std::array<float,16> result{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double value=0;for(unsigned k=0;k<4;++k)value+=double(local[r*4+k])*parent[k*4+c];
        result[r*4+c]=static_cast<float>(value);if(!std::isfinite(result[r*4+c]))return false;
    }
    output=result;return true;
}
inline bool TransformBounds(const std::array<float,3>& low,const std::array<float,3>& high,const float* pose,
                            std::array<float,3>& minimum,std::array<float,3>& maximum)
{
    std::array<float,3> lo{},hi{};
    for(unsigned corner=0;corner<8;++corner)for(unsigned a=0;a<3;++a){
        double value=pose[12+a];
        for(unsigned k=0;k<3;++k)value+=(corner&(1u<<k)?high[k]:low[k])*double(pose[k*4+a]);
        const float p=static_cast<float>(value);if(!std::isfinite(p))return false;
        if(!corner)lo[a]=hi[a]=p;else{lo[a]=std::min(lo[a],p);hi[a]=std::max(hi[a],p);}
    }
    minimum=lo;maximum=hi;return true;
}
struct PlacementPose
{
    std::array<float, 3> position{}, rotation{}; // original MDDF coordinates/degrees
    uint16_t scale = 1024;
};

inline std::array<float,2> PlacementOriginXY(const PlacementPose& pose)
{
    constexpr float origin = 17066.666666666668f;
    return {origin-pose.position[2],origin-pose.position[0]};
}

// Static rigid placement, independently checked against captured native root
// matrices, including a two-axis tilt. Row vectors; translation occupies 12..14.
// Bone animation/billboarding is not implied. Failure leaves output unchanged.
inline bool PlacementMatrix(const PlacementPose& pose, std::array<float, 16>& output)
{
    for (float value : pose.position) if (!std::isfinite(value)) return false;
    for (float value : pose.rotation) if (!std::isfinite(value)) return false;
    if (!pose.scale) return false;
    constexpr double radians = 3.14159265358979323846/180;
    const double angles[] = {double(pose.rotation[2])*radians, double(pose.rotation[0])*radians,
                             (double(pose.rotation[1])-180)*radians};
    double result[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const double c = std::cos(angles[axis]), s = std::sin(angles[axis]);
        double rotation[3][3]{};
        rotation[axis][axis] = 1;
        const unsigned a = (axis+1)%3, b = (axis+2)%3;
        rotation[a][a] = rotation[b][b] = c;
        rotation[a][b] = s; rotation[b][a] = -s;
        double next[3][3]{};
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col)
                for (unsigned k = 0; k < 3; ++k) next[row][col] += result[row][k]*rotation[k][col];
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col) result[row][col] = next[row][col];
    }
    std::array<float, 16> candidate{};
    const double scale = double(pose.scale)/1024;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col) candidate[row*4+col] = static_cast<float>(result[row][col]*scale);
    // Native float rounding matters here; the initial double-precision survey
    // differs by about 0.001 yd. MCNK terrain positions are already world space.
    const auto xy=PlacementOriginXY(pose);
    candidate[12] = xy[0];
    candidate[13] = xy[1];
    candidate[14] = pose.position[1]; candidate[15] = 1;
    for (float value : candidate) if (!std::isfinite(value)) return false;
    output = candidate;
    return true;
}
}
