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
    // blend
    const float b100=blend(100,1);
    assert(blend(0,1)==0&&blend(100,0)==0&&blend(100,-1)==0&&blend(100,nan)==0&&blend(100,inf)==0);
    assert(b100==1&&blend(150,1)==1&&blend(100,2)==1&&std::fabs(blend(50,.5f)-.25f)<1e-6f);
    // The composite chain for one pixel (bloom/AO already in original, relight, then the limit), as WorldComposite computes it.
    struct Px{float original[3],fog[3],baseline[3],bounce[3],legacyT,ambient[3];};
    auto chain=[&](const Px& p,float x,float* o){
        for(int i=0;i<3;++i){const float oldLight=std::max(p.baseline[i],.15f),fogPart=std::min(p.fog[i],p.original[i]);
            const float transported=std::max(p.original[i]-fogPart,0.f);
            const float albedoT=std::min(transported/oldLight,p.legacyT);
            float color=p.original[i]+albedoT*p.bounce[i];
            color=std::max(color,p.original[i]-.45f*transported);
            const float stock=albedoT*(p.ambient[i]*ArtLayerNightAmbient[i])+p.original[i];
            o[i]=x*std::max(stock-color,0.f)+color;}
    };
    auto px=[](float a,float b,float c,float bounce){Px p={{a,b,c},{0,0,0},{.15f,.15f,.15f},{bounce,bounce,bounce},1,{0,0,0}};return p;};
    // Moon shadow / dark GI (negative bounce): 0 keeps the relit night, 100 gives the game's own picture, linear between.
    {const Px shade=px(.03f,.04f,.06f,-.1f);float relit[3],o[3];chain(shade,0,relit);
     for(int i=0;i<3;++i)assert(relit[i]<shade.original[i]);
     float prev=relit[1];for(unsigned pct:{25u,50u,75u,100u}){chain(shade,blend(pct,1),o);assert(o[1]>prev);prev=o[1];
        for(int i=0;i<3;++i){const float want=relit[i]+pct/100.f*(shade.original[i]-relit[i]);assert(std::fabs(o[i]-want)<1e-6f);}}
     chain(shade,b100,o);for(int i=0;i<3;++i)assert(o[i]==shade.original[i]);}
    // Added light (lamp, positive bounce) and the bloom glow in original are never touched; nothing exceeds max(relit, original).
    {const Px lamp=px(.3f,.25f,.1f,1.f),glow=px(.9f,.6f,.2f,0);float a[3],o[3];
     for(const Px* p:{&lamp,&glow}){chain(*p,0,a);chain(*p,b100,o);for(int i=0;i<3;++i)assert(o[i]==a[i]&&o[i]<=std::max(a[i],p->original[i]));}}
    // Monotonic in the relit value; shadowed never above its lit neighbour.
    for(float x:{.25f,.5f,1.f})for(float lit:{.05f,.2f,.5f,.9f})for(float amt:{.01f,.05f,.2f}){
        float oa[3],ob[3];chain(px(.3f,.3f,.3f,lit),x,oa);chain(px(.3f,.3f,.3f,lit-amt),x,ob);assert(ob[1]<=oa[1]);
        chain(px(.3f,.3f,.3f,-lit),x,oa);chain(px(.3f,.3f,.3f,-lit-amt),x,ob);assert(ob[1]<=oa[1]);}
    // Off is bit-identical; pure fog unchanged.
    {const Px shade=px(.03f,.04f,.06f,-.1f);float o[3],ref[3];
     for(int i=0;i<3;++i){const float color=std::max(shade.original[i]+shade.original[i]/.15f*shade.bounce[i],shade.original[i]-.45f*shade.original[i]);ref[i]=color;}
     chain(shade,blend(0,1),o);for(int i=0;i<3;++i)assert(o[i]==ref[i]);
     chain(shade,blend(100,0),o);for(int i=0;i<3;++i)assert(o[i]==ref[i]);
     Px fogOnly=shade;for(int i=0;i<3;++i){fogOnly.original[i]=.2f;fogOnly.fog[i]=.3f;}
     chain(fogOnly,b100,o);for(int i=0;i<3;++i)assert(o[i]==.2f);}
    // The art layer's night ambient: at 100 the ambient part comes back to the stock level (albedo x stock ambient), cool tint undone.
    {Px dark=px(.06f,.07f,.08f,0);const float amb[3]={.05f,.06f,.08f};for(int i=0;i<3;++i){dark.ambient[i]=amb[i];dark.baseline[i]=.1f;}
     float o[3],half[3];chain(dark,b100,o);chain(dark,.5f,half);
     for(int i=0;i<3;++i){const float albedo=dark.original[i]/.15f,want=dark.original[i]+albedo*amb[i]*ArtLayerNightAmbient[i];
        assert(std::fabs(o[i]-want)<1e-6f&&o[i]>dark.original[i]);assert(std::fabs(half[i]-(dark.original[i]+want)/2)<1e-6f);}
     assert(o[0]/dark.original[0]>o[2]/dark.original[2]);   // red, cut most by the cool tint, comes back most
     chain(dark,0,o);for(int i=0;i<3;++i)assert(o[i]==dark.original[i]);}
    std::printf("PASS night floor maxStep=%.6f full=%u zero=%u seconds; weight ramp, orbit sweep, smoothing, blend, darkening limit\n",maxStep,full,zero);
}
