#pragma STDC FENV_ACCESS ON
#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-wave2-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include "spell_fog_march.h"
#include <cassert>
#include <cfenv>
#include <cstring>
#include <limits>
#include <random>
#include <vector>
int main(){std::mt19937 rng(48201);std::vector<uint8_t> volume(64*64*64);for(auto& x:volume)x=uint8_t(rng());
    auto same=[&](float x,float y,float z){const float a=OldFogClouds::sample(volume.data(),x,y,z),b=NorthlightFogClouds::sample(volume.data(),x,y,z);assert(!std::memcmp(&a,&b,4));};
    for(unsigned i=0;i<1000000;++i){float c[3];for(auto& x:c){uint32_t bits=rng();std::memcpy(&x,&bits,4);if(!std::isfinite(x))x=0;}same(c[0],c[1],c[2]);}
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);
        for(float x:{-std::numeric_limits<float>::max(),-8388609.f,-8388608.f,-1.f,-0.f,0.f,1.f,8388608.f,8388609.f,std::numeric_limits<float>::max()})
            for(float y:{std::nextafter(x,-INFINITY),x,std::nextafter(x,INFINITY)})if(std::isfinite(y))same(x,y,.37f);
        for(unsigned i=0;i<10000;++i){float x=float(int(rng()%100000)-50000)/997.f,y=float(int(rng()%100000)-50000)/1003.f,z=float(int(rng()%100000)-50000)/991.f;same(x,y,z);}}
    assert(std::fesetround(FE_TONEAREST)==0);
    NorthlightRegionalFog::Field field;field.originX=field.originY=-256;for(unsigned i=0;i<NorthlightRegionalFog::N*NorthlightRegionalFog::N;++i)field.texels[i]={float(int(i%23)-10),.02f,.01f,i%7?5.f:0.f};
    NorthlightSpellFog::March march;march.night=.5f;march.volume[0]=.02f;march.volume[1]=.01f;march.volume[2]=60;march.volume[3]=30;
    const float from[3]={0,0,4};OldFogClouds::Frame a;a.active=true;a.invLarge=1.f/192;a.invSmall=1.f/48;a.height=12;a.sharpness=2;a.threshold=.5f;a.sigmaMax=.03f;NorthlightFogClouds::Frame b;static_assert(sizeof a==sizeof b);std::memcpy(&b,&a,sizeof a);
    auto old=[&](float x,float y,float z,const auto& s){const float p[]={x,y,z};return OldFogClouds::sigmaAt(volume.data(),a,from,p,s.ground,s.height,s.coverage);};
    auto now=[&](float x,float y,float z,const auto& s){const float p[]={x,y,z};return NorthlightFogClouds::sigmaAt(volume.data(),b,from,p,s.ground,s.height,s.coverage);};
    for(unsigned i=0;i<10000;++i){float to[]={float(i%256),float(i%128),float(i%32)},p,q,r,s;NorthlightSpellFog::opticalDepthPair(field,march,from,to,24,old,p,q);NorthlightSpellFog::opticalDepthPair(field,march,from,to,24,now,r,s);assert(!std::memcmp(&p,&r,4)&&!std::memcmp(&q,&s,4));}
}
