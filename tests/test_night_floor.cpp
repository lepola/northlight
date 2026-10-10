#include "night_floor.h"
#include "celestial_time_warp.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightNightFloor;
using namespace NorthlightCelestialOrbit;
static float sunZ(double seconds){return evaluate(seconds/kDaySeconds).sun.direction[2];}
static float at(double seconds){auto o=evaluate(seconds/kDaySeconds);return weight(o.valid,o.sun.direction[2]);}
int main(){
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    assert(weight(false,-1)==0&&weight(false,0)==0);
    assert(weight(true,nan)==0&&weight(true,inf)==0&&weight(true,-inf)==0);
    assert(weight(true,1.01f)==0&&weight(true,-1.01f)==0);
    assert(weight(true,StartZ)==0&&weight(true,EndZ)==1&&weight(true,1)==0&&weight(true,-1)==1);
    assert(weight(true,(StartZ+EndZ)/2)>.4f&&weight(true,(StartZ+EndZ)/2)<.6f);
    // Full-day sweep over the real orbit.
    float previous=at(0),maxStep=0;unsigned full=0,zero=0;
    assert(at(0)==1&&at(12*3600)==0);
    for(unsigned second=1;second<86400;++second){
        float w=at(second);assert(w>=0&&w<=1);
        const double elevation=elevationDegrees(sunZ(second));
        if(elevation>=4)assert(w==0);
        if(elevation<=-8)assert(w==1);
        if(w==0)++zero;if(w==1)++full;
        maxStep=std::max(maxStep,std::fabs(w-previous));previous=w;
    }
    assert(zero>0&&full>0);
    // Measured steepest step 0.000515 per game second on the native orbit; bound with ~20% headroom.
    assert(maxStep<.00062f);
    // Dawn rises monotonically out of the night, dusk falls into it.
    previous=at(0);
    for(double t=0;t<=12*3600;t+=.5){float w=at(t);assert(w<=previous+1e-7f);previous=w;}
    previous=at(12*3600);
    for(double t=12*3600;t<=24*3600-1;t+=.5){float w=at(t);assert(w+1e-7f>=previous);previous=w;}
    // Same held-clock smoothing as source light, at multiple frame rates, around sunset and sunrise.
    for(double centre:{kSunsetSeconds,kSunriseSeconds})for(unsigned fps:{30u,60u,144u}){
        LightMotion filter;float last=0;bool started=false;
        for(unsigned f=0;f<fps*7200;++f){double day=(centre-3600+double(f)/fps)/kDaySeconds;
            auto o=filter.update(evaluate(day),day,uint32_t(1000.*f/fps));
            float w=weight(true,o.sun.direction[2]);assert(w>=0&&w<=1);
            if(started)assert(std::fabs(w-last)<.0001f);last=w;started=true;
        }
    }
    // constants
    const float amb[3]={.1f,.2f,.3f};float k[2];
    constants(0,1,amb,k);assert(k[0]==0&&k[1]==0);
    constants(100,0,amb,k);assert(k[0]==0&&k[1]==0);
    constants(100,-1,amb,k);assert(k[0]==0&&k[1]==0);
    constants(100,nan,amb,k);assert(k[0]==0&&k[1]==0);
    const float nanAmb[3]={.1f,nan,.3f},dark[3]={0,0,0},tiny[3]={1e-5f,1e-5f,1e-5f};
    constants(100,1,nanAmb,k);assert(k[0]==0&&k[1]==0);
    constants(100,1,dark,k);assert(k[0]==0&&k[1]==0);
    constants(100,1,tiny,k);assert(k[0]==0&&k[1]==0);
    constants(100,1,amb,k);assert(std::fabs(k[0]-1/(3*1.5f*.2f))<1e-4f&&k[1]==2);
    float cap[2];constants(150,1,amb,cap);assert(cap[0]==k[0]&&cap[1]==k[1]);
    constants(50,.5f,amb,cap);assert(std::fabs(cap[1]-.5f)<1e-6f);
    // The composite chain for one pixel (relight, then the lift), as WorldComposite computes it.
    struct Px{float original[3],fog[3],baseline[3],bounce[3],legacyT;};
    auto chain=[&](const Px& p,const float* kk,float* o){
        float oldLight[3],transported[3],color[3],fogPart[3];
        for(int i=0;i<3;++i){oldLight[i]=std::max(p.baseline[i],.15f);fogPart[i]=std::min(p.fog[i],p.original[i]);
            transported[i]=std::max(p.original[i]-fogPart[i],0.f);
            color[i]=p.original[i]+std::min(transported[i]/oldLight[i],p.legacyT)*p.bounce[i];
            color[i]=std::max(color[i],p.original[i]-.45f*transported[i]);}
        lift(color,fogPart,oldLight,kk,o);
    };
    auto lum=[](const float*v){return (v[0]+v[1]+v[2])/3;};
    const float nAmb[3]={5/255.f,10/255.f,30/255.f};
    const Px dim={{.03f,.04f,.06f},{0,0,0},{.15f,.15f,.15f},{0,0,0},1};
    // Northrend-like night: monotonic, >= +30% at 25, hue kept.
    {float prev=lum(dim.original);
     for(unsigned pct:{25u,50u,75u,100u}){float kk[2],o[3];constants(pct,1,nAmb,kk);chain(dim,kk,o);
        assert(lum(o)>prev);prev=lum(o);if(pct==25)assert(lum(o)>=1.3f*lum(dim.original));
        assert(std::fabs(o[0]/o[1]-dim.original[0]/dim.original[1])<1e-5f&&std::fabs(o[2]/o[1]-dim.original[2]/dim.original[1])<1e-5f);}}
    // Shadow keeps its ratio to the lit neighbour (same baseline, negative bounce), never inverted.
    {Px lit=dim,sh=dim;for(int i=0;i<3;++i){lit.bounce[i]=.05f;sh.bounce[i]=-.05f;lit.original[i]=sh.original[i]=.3f;}
     for(unsigned pct:{0u,25u,50u,100u}){float kk[2],a[3],b[3];constants(pct,1,nAmb,kk);chain(lit,kk,a);chain(sh,kk,b);
        float l0[3],s0[3],z[2]={0,0};chain(lit,z,l0);chain(sh,z,s0);
        assert(b[1]<a[1]&&std::fabs(b[1]/a[1]-s0[1]/l0[1])<1e-5f);}}
    // Very dark night (ambient lum .03, baseline at the clamp): full lift is 1+Gain, monotonic.
    {const float darkAmb[3]={.03f,.03f,.03f};float prev=1,kk[2],o[3];
     for(unsigned pct:{25u,50u,75u,100u}){constants(pct,1,darkAmb,kk);chain(dim,kk,o);float g=o[1]/dim.original[1];assert(g>prev);prev=g;
        if(pct==100)assert(std::fabs(g-(1+Gain))<1e-5f);}}
    // Off cases bit-identical, pure fog unchanged.
    {float o[3],kk[2],z[2]={0,0};
     constants(0,1,nAmb,kk);chain(dim,kk,o);for(int i=0;i<3;++i)assert(o[i]==dim.original[i]);
     constants(100,0,nAmb,kk);chain(dim,kk,o);for(int i=0;i<3;++i)assert(o[i]==dim.original[i]);
     chain(dim,z,o);for(int i=0;i<3;++i)assert(o[i]==dim.original[i]);
     Px fogOnly=dim;for(int i=0;i<3;++i){fogOnly.original[i]=.2f;fogOnly.fog[i]=.3f;}
     constants(100,1,nAmb,kk);chain(fogOnly,kk,o);for(int i=0;i<3;++i)assert(o[i]==.2f);
     // lamp-lit pixel (baseline 10x the reference light) gets < 1/5 of the dark pixel's relative lift
     Px lamp=dim;for(int i=0;i<3;++i)lamp.baseline[i]=10*MinLight; // L is the .15 clamp here
     float d[3],l[3];chain(dim,kk,d);chain(lamp,kk,l);
     assert((l[1]/lamp.original[1]-1)<.2f*(d[1]/dim.original[1]-1));}
    std::printf("PASS night floor maxStep=%.6f full=%u zero=%u seconds; weight ramp, orbit sweep, smoothing, constants, lift\n",maxStep,full,zero);
}
