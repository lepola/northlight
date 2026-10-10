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
    float k[2];
    constants(0,1,k);assert(k[0]==0&&k[1]==0);
    constants(100,0,k);assert(k[0]==0&&k[1]==0);
    constants(100,-1,k);assert(k[0]==0&&k[1]==0);
    constants(100,nan,k);assert(k[0]==0&&k[1]==0);
    constants(100,inf,k);assert(k[0]==0&&k[1]==0);
    constants(100,1,k);assert(std::fabs(k[0]-1/(3*Target))<1e-4f&&k[1]==2);
    float cap[2];constants(150,1,cap);assert(cap[0]==k[0]&&cap[1]==k[1]);
    constants(50,.5f,cap);assert(std::fabs(cap[1]-.5f)<1e-6f);
    // The composite chain for one pixel (relight, then the lift), as WorldComposite computes it.
    struct Px{float original[3],fog[3],baseline[3],bounce[3],legacyT;};
    auto chain=[&](const Px& p,const float* kk,float* o){
        float oldLight[3],transported[3],color[3],fogPart[3];
        for(int i=0;i<3;++i){oldLight[i]=std::max(p.baseline[i],.15f);fogPart[i]=std::min(p.fog[i],p.original[i]);
            transported[i]=std::max(p.original[i]-fogPart[i],0.f);
            color[i]=p.original[i]+std::min(transported[i]/oldLight[i],p.legacyT)*p.bounce[i];
            color[i]=std::max(color[i],p.original[i]-.45f*transported[i]);}
        lift(color,fogPart,kk,o);
    };
    auto lum=[](const float*v){return (v[0]+v[1]+v[2])/3;};
    const float off2[2]={0,0};
    auto px=[](float a,float b,float c,float bounce){Px p={{a,b,c},{0,0,0},{.15f,.15f,.15f},{bounce,bounce,bounce},1};return p;};
    const Px dim=px(.03f,.04f,.06f,0);
    // Dark pixel (below Target): gain exactly 1+A, strictly monotonic, hue kept.
    {float prev=1;for(unsigned pct:{25u,50u,75u,100u}){float kk[2],o[3];constants(pct,1,kk);chain(dim,kk,o);
        const float g=lum(o)/lum(dim.original);assert(g>prev);prev=g;
        assert(std::fabs(g-(1+kk[1]))<1e-5f);
        if(pct==100)assert(std::fabs(g-(1+Gain))<1e-5f);
        assert(std::fabs(o[0]/o[1]-dim.original[0]/dim.original[1])<1e-5f&&std::fabs(o[2]/o[1]-dim.original[2]/dim.original[1])<1e-5f);}}
    // Lamp (large positive bounce, baseline at the clamp) and baked bright WMO pixel: small relative lift, bounded absolute add.
    {float kk[2],d[3],o[3];constants(100,1,kk);chain(dim,kk,d);const float dimRel=lum(d)/lum(dim.original)-1;
     Px lamp=px(.3f,.3f,.3f,1.f),baked=px(.6f,.6f,.6f,0);
     for(const Px* p:{&lamp,&baked}){float base[3];chain(*p,off2,base);chain(*p,kk,o);
        assert(lum(o)/lum(base)-1<.25f*dimRel);assert(lum(o)-lum(base)<=Gain*Target*(1+1e-5f));}}
    // Monotonic in surface brightness; shadowed never above its lit neighbour.
    for(unsigned pct:{25u,50u,100u}){float kk[2];constants(pct,1,kk);float prev=-1;
        for(unsigned i=0;i<=100;++i){float v=i/100.f,c[3]={v,v,v},f[3]={0,0,0},o[3];lift(c,f,kk,o);assert(o[0]>=prev);prev=o[0];}
        for(float lit:{.05f,.2f,.5f,.9f})for(float amt:{.01f,.05f,.2f}){Px a=px(.3f,.3f,.3f,lit),b=px(.3f,.3f,.3f,lit-amt);
            float oa[3],ob[3];chain(a,kk,oa);chain(b,kk,ob);assert(ob[1]<=oa[1]);}}
    // Off cases bit-identical, pure fog unchanged.
    {float o[3],kk[2];
     constants(0,1,kk);chain(dim,kk,o);for(int i=0;i<3;++i)assert(o[i]==dim.original[i]);
     constants(100,0,kk);chain(dim,kk,o);for(int i=0;i<3;++i)assert(o[i]==dim.original[i]);
     Px fogOnly=dim;for(int i=0;i<3;++i){fogOnly.original[i]=.2f;fogOnly.fog[i]=.3f;}
     constants(100,1,kk);chain(fogOnly,kk,o);for(int i=0;i<3;++i)assert(o[i]==.2f);}
    std::printf("PASS night floor maxStep=%.6f full=%u zero=%u seconds; weight ramp, orbit sweep, smoothing, constants, lift\n",maxStep,full,zero);
}
