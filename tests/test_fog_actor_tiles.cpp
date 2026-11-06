#include "actor_shadow_map.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main(){
    using NorthlightActorShadowMap::tiledFogRange;
    float m[16]={};m[0]=m[5]=1.f/12;m[10]=1.f/1280;m[15]=1;
    float eye[3]={};assert(tiledFogRange(m,eye,.0001f));
    eye[0]=255;assert(tiledFogRange(m,eye,0));eye[0]=257;assert(!tiledFogRange(m,eye,0));eye[0]=0;
    assert(!tiledFogRange(nullptr,eye,0)&&!tiledFogRange(m,nullptr,0));
    assert(!tiledFogRange(m,eye,-.001f)&&!tiledFogRange(m,eye,INFINITY));
    eye[1]=std::numeric_limits<float>::quiet_NaN();assert(!tiledFogRange(m,eye,0));eye[1]=0;
    m[14]=INFINITY;assert(!tiledFogRange(m,eye,0));m[14]=0;
    eye[0]=1e20f;m[12]=-eye[0]*m[0];assert(!tiledFogRange(m,eye,0));eye[0]=m[12]=0;
    unsigned seed=91571,cases=0;auto random=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216;};
    for(unsigned i=0;i<1000;++i){
        const float angle=random()*6.2831853f;
        m[0]=std::cos(angle)/12;m[4]=std::sin(angle)/12;m[1]=-m[4];m[5]=m[0];
        eye[0]=(random()-.5f)*34000;eye[1]=(random()-.5f)*34000;eye[2]=random()*4000;
        m[12]=-(eye[0]*m[0]+eye[1]*m[4]);m[13]=-(eye[0]*m[1]+eye[1]*m[5]);
        assert(tiledFogRange(m,eye,.0001f));
        for(unsigned ray=0;ray<32;++ray){
            double d[3]={random()-.5,random()-.5,random()-.5};
            const double scale=128/std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            for(unsigned axis=0;axis<2;++axis){
                double p=m[12+axis];for(unsigned k=0;k<3;++k)p+=(eye[k]+d[k]*scale)*m[4*k+axis];
                assert(std::fabs(p)<=32);++cases;
            }
        }
    }
    std::printf("PASS tiled fog coordinate guard: %u endpoints, invalid matrices/cameras, negative bias and supported-range boundary\n",cases);
}
