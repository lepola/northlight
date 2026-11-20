#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace reflection {
// World-space n.p=d, unit upward normal. Anchor-relative calculations avoid
// losing the slope at large world coordinates. Keys identify native wet tiles,
// not their position in the camera-dependent submission list.
struct WaterSurface {
    uint64_t key=0;
    std::array<double,3> normal={0,0,1},anchor={};
    double distance=0,residual=0,area=0;
    std::array<float,6> bounds={};
    // Preserve the authored quad for raster ownership. A rotated building's
    // liquid covers less than its axis-aligned box; the box is only for queries.
    std::array<std::array<float,3>,4> corners={};
    bool cornersKnown=false;
};
inline bool FitWaterTile(const std::array<std::array<double,3>,4>& p,WaterSurface& out,double minimumUp=.25) {
    for(const auto& v:p)for(double c:v)if(!std::isfinite(c))return false;
    // Opposite-edge averages fit a regular quad without inventing a slope
    // from its axis-aligned bounds. The fourth vertex is checked too.
    std::array<double,3> u={},v={},n={};
    for(int c=0;c<3;++c){
        u[c]=(p[1][c]-p[0][c]+p[3][c]-p[2][c])*.5;
        v[c]=(p[2][c]-p[0][c]+p[3][c]-p[1][c])*.5;
        out.anchor[c]=(p[0][c]+p[1][c]+p[2][c]+p[3][c])*.25;
        out.bounds[c]=out.bounds[c+3]=float(p[0][c]);
        for(const auto& q:p){out.bounds[c]=std::min(out.bounds[c],float(q[c]));out.bounds[c+3]=std::max(out.bounds[c+3],float(q[c]));}
    }
    n={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(!std::isfinite(length)||length<1e-8)return false;
    out.area=length;out.distance=0;out.residual=0;
    for(int c=0;c<3;++c){out.normal[c]=n[c]*(n[2]<0?-1:1)/length;out.distance+=out.normal[c]*out.anchor[c];}
    if(out.normal[2]<minimumUp)return false;
    for(const auto& q:p){double error=0;for(int c=0;c<3;++c)error+=out.normal[c]*(q[c]-out.anchor[c]);out.residual=std::max(out.residual,std::fabs(error));}
    for(unsigned i=0;i<4;++i)for(unsigned c=0;c<3;++c)out.corners[i][c]=float(p[i][c]);
    out.cornersKnown=true;
    return true;
}
}
