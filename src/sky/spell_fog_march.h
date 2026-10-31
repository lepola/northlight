#pragma once
// Task 34 SpellFog: the CPU mirror of WorldFog's extinction (shaders/world_effects.hlsl, WorldFog), term by term, for the optical depth Northlight's fog puts between the eye and a point.
// Same inputs the GPU gets this frame: the regional field (bilinear, valid cells only, the border fade as the coverage), the night fraction (RegionalFogInfo.w), VolumeAir (c32), the
// generic-forest air (FogColor.w), the shared air extinction with the rain's extra (WeatherInfo.w) and the near fade (FogRange). Pure: no D3D.
//   ground   = max(field.day + field.nightExtra * night, 0) * vertical^2,   vertical = saturate(1 - altitude / lerp(height, 6, night * (1 - profile)))
//   air      = (airFloor + lerp(lerp(volume.y, volume.x, profile), forestAir, generalForest) * saturate(1.6 * height - 1)) * airVertical^2
//   sigma    = max(ground + air, 0) * (altitude >= 0 ? coverage : 0) * nearFade(distance along the ray)
// altitude = z - field ground at the sample (so the heights are relative to the ground at every step, as in the shader).
#include <algorithm>
#include <cmath>
#include "regional_fog.h"
namespace NorthlightSpellFog {
struct March{
    float night=0,volume[4]={},forestAir=0,airFloor=.0017f;   // c31.w, c32, c22.w, c59.w
    float nearStart=3.5f,nearInv=1.f/12;                       // c58.x, c58.y
};
inline float saturate(float v){return std::min(1.f,std::max(0.f,v));}
// The field at (x, y) as regionalFogAt returns it: bilinear weights, the ground and height over the valid cells only, day and night density over all four, coverage = valid weight x border fade.
struct FieldSample{float ground=0,day=0,nightExtra=0,height=0,coverage=0;};
inline FieldSample fieldAt(const NorthlightRegionalFog::Field& f,float x,float y){
    constexpr int N=int(NorthlightRegionalFog::N);
    const float gx=(x-f.originX)/NorthlightRegionalFog::Spacing,gy=(y-f.originY)/NorthlightRegionalFog::Spacing;
    FieldSample s;
    if(!(gx>=0&&gy>=0&&gx<=N-1&&gy<=N-1))return s;                      // outside: the border fade is 0 there
    const int bx=std::min(int(gx),N-2),by=std::min(int(gy),N-2);const float fx=gx-float(bx),fy=gy-float(by);
    const float w[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    const NorthlightRegionalFog::Texel* t[4]={&f.texels[size_t(by)*N+size_t(bx)],&f.texels[size_t(by)*N+size_t(bx)+1],&f.texels[size_t(by+1)*N+size_t(bx)],&f.texels[size_t(by+1)*N+size_t(bx)+1]};
    float total=0,ground=0,height=0;
    for(int i=0;i<4;++i){
        s.day+=w[i]*t[i]->day;s.nightExtra+=w[i]*t[i]->nightExtra;
        if(t[i]->height>0){total+=w[i];ground+=w[i]*t[i]->ground;height+=w[i]*t[i]->height;}
    }
    const float inv=1.f/std::max(total,1e-6f);
    s.ground=ground*inv;s.height=height*inv;
    const float edge=saturate(std::min(std::min(gx,gy),std::min(float(N-1)-gx,float(N-1)-gy))*.5f);
    s.coverage=total*edge*edge*(3-2*edge);
    return s;
}
// WorldFog's sigma at a world point before the near fade; also returns the field sample (the cloud term needs ground, height and coverage).
inline float sigma(const NorthlightRegionalFog::Field& f,const March& m,float x,float y,float z,FieldSample& s){
    s=fieldAt(f,x,y);
    const float altitude=z-s.ground;
    const float profile=saturate((s.height-2.5f)/2.5f),generalForest=1-saturate((s.height-1.25f)/1.25f);
    const float groundHeight=s.height+(6-s.height)*(m.night*(1-profile));
    const float vertical=saturate(1-altitude/std::max(groundHeight,.001f));
    const float valid=altitude>=0?s.coverage:0;
    const float groundSigma=std::max(s.day+s.nightExtra*m.night,0.f)*vertical*vertical;
    const float airHeight=m.volume[3]+(m.volume[2]-m.volume[3])*profile;
    const float airVertical=saturate(1-altitude/std::max(airHeight,.001f));
    float airBase=m.volume[1]+(m.volume[0]-m.volume[1])*profile;
    airBase=airBase+(m.forestAir-airBase)*generalForest;
    airBase=m.airFloor+airBase*saturate(s.height*1.6f-1);
    const float airSigma=airBase*airVertical*airVertical;
    return std::max(groundSigma+airSigma,0.f)*valid;
}
inline float nearFade(const March& m,float distance){float n=saturate((distance-m.nearStart)*m.nearInv);return n*n*(3-2*n);}
// The optical depth from `from` to `to` (a straight ray), `steps` equal midpoint samples, the near fade by distance from `from`. extra(x, y, z, sample) adds a second density at the same sample (the fog
// clouds' bank mean): its result is added to sigma before the fade, zero when it is not given.
template<class Extra> inline float opticalDepth(const NorthlightRegionalFog::Field& f,const March& m,const float from[3],const float to[3],unsigned steps,Extra&& extra){
    float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(!(length>1e-4f)||!steps)return 0;
    const float step=length/float(steps);float depth=0;
    for(unsigned i=0;i<steps;++i){
        const float t=(float(i)+.5f)*step,u=t/length;
        const float x=from[0]+d[0]*u,y=from[1]+d[1]*u,z=from[2]+d[2]*u;
        FieldSample s;const float base=sigma(f,m,x,y,z,s);
        depth+=(base+extra(x,y,z,s))*nearFade(m,t)*step;
    }
    return depth;
}
// A known global footprint bound proves that a whole ray stays above every cloud bank.
// The four-unit margin covers float interpolation and world-coordinate rounding;
// unknown or out-of-range values retain the full cloud march.
inline bool cloudRayAboveSupport(const std::array<float,3>& bounds,float height,float fromZ,float toZ){
    if(!(bounds[2]>0&&std::isfinite(bounds[0])&&std::isfinite(bounds[1])&&std::isfinite(bounds[2])&&
         bounds[0]<=bounds[1]&&std::fabs(bounds[0])<=100000&&std::fabs(bounds[1])<=100000&&
         std::isfinite(height)&&height>0&&height<=20&&std::isfinite(fromZ)&&std::isfinite(toZ)&&
         std::fabs(fromZ)<=100000&&std::fabs(toZ)<=100000))return false;
    return std::min(fromZ,toZ)>bounds[1]+5.5f*height+4.f;
}
// Compute the clear and cloud optical depths over the same samples.
template<class Extra> inline void opticalDepthPair(const NorthlightRegionalFog::Field& f,const March& m,const float from[3],const float to[3],unsigned steps,Extra&& extra,float& clear,float& cloudy){
    float d[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    clear=cloudy=0;
    if(!(length>1e-4f)||!steps)return;
    const float step=length/float(steps);
    for(unsigned i=0;i<steps;++i){
        const float t=(float(i)+.5f)*step,u=t/length;
        const float x=from[0]+d[0]*u,y=from[1]+d[1]*u,z=from[2]+d[2]*u;
        FieldSample s;const float base=sigma(f,m,x,y,z,s);
        clear+=(base+0.f)*nearFade(m,t)*step;
        cloudy+=(base+extra(x,y,z,s))*nearFade(m,t)*step;
    }
}
}
