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
    // The composite chain for one pixel (bloom/AO already in original, relight, then the ambient), as WorldComposite computes it.
    struct Px{float original[3],fog[3],baseline[3],bounce[3],legacyT,ambient[3];};
    auto chain=[&](const Px& p,float x,float* o){
        for(int i=0;i<3;++i){const float oldLight=std::max(p.baseline[i],.15f),fogPart=std::min(p.fog[i],p.original[i]);
            const float transported=std::max(p.original[i]-fogPart,0.f);
            const float albedoT=std::min(transported/oldLight,p.legacyT);
            float color=p.original[i]+albedoT*p.bounce[i];
            color=std::max(color,p.original[i]-.45f*transported);
            o[i]=(albedoT*x)*(p.ambient[i]*ArtLayerNightAmbient[i])+color;}
    };
    const float amb[3]={.05f,.06f,.08f};
    auto px=[&](float a,float b,float c,float bounce){Px p={{a,b,c},{0,0,0},{.15f,.15f,.15f},{bounce,bounce,bounce},1,{amb[0],amb[1],amb[2]}};return p;};
    // A dark surface: at 100 the ambient part is back at the stock level (albedo x stock ambient), linear in the setting, cool tint undone.
    {const Px dark=px(.06f,.07f,.08f,0);float o[3],half[3];chain(dark,b100,o);chain(dark,.5f,half);
     for(int i=0;i<3;++i){const float albedo=dark.original[i]/.15f,want=dark.original[i]+albedo*amb[i]*ArtLayerNightAmbient[i];
        assert(std::fabs(o[i]-want)<1e-6f&&o[i]>dark.original[i]);assert(std::fabs(half[i]-(dark.original[i]+want)/2)<1e-6f);}
     assert(o[0]/dark.original[0]>o[2]/dark.original[2]);}   // red, cut most by the cool tint, comes back most
    // Moon shadow (negative bounce) next to its lit neighbour: both get the same added light, so the shadow keeps its absolute contrast.
    for(float x:{.25f,.5f,1.f})for(float amt:{.01f,.05f,.1f}){
        const Px lit=px(.1f,.1f,.1f,0),shade=px(.1f,.1f,.1f,-amt);float a0[3],b0[3],a[3],b[3];
        chain(lit,0,a0);chain(shade,0,b0);chain(lit,x,a);chain(shade,x,b);
        for(int i=0;i<3;++i){assert(b0[i]<a0[i]&&b[i]<a[i]);assert(std::fabs((a[i]-b[i])-(a0[i]-b0[i]))<1e-6f);}}
    // A lamp-lit surface gets only the same small ambient add; its relit light is kept.
    {const Px lamp=px(.3f,.25f,.1f,1.f);float a[3],o[3];chain(lamp,0,a);chain(lamp,b100,o);
     for(int i=0;i<3;++i){assert(o[i]>=a[i]);assert(o[i]-a[i]<=lamp.original[i]/.15f*amb[i]*ArtLayerNightAmbient[i]+1e-6f);}}
    // Off is bit-identical; pure fog unchanged.
    {const Px shade=px(.03f,.04f,.06f,-.1f);float o[3],ref[3];
     for(int i=0;i<3;++i)ref[i]=std::max(shade.original[i]+shade.original[i]/.15f*shade.bounce[i],shade.original[i]-.45f*shade.original[i]);
     chain(shade,blend(0,1),o);for(int i=0;i<3;++i)assert(o[i]==ref[i]);
     chain(shade,blend(100,0),o);for(int i=0;i<3;++i)assert(o[i]==ref[i]);
     Px fogOnly=shade;for(int i=0;i<3;++i){fogOnly.original[i]=.2f;fogOnly.fog[i]=.3f;}
     chain(fogOnly,b100,o);for(int i=0;i<3;++i)assert(o[i]==.2f);}
    std::printf("PASS night floor maxStep=%.6f full=%u zero=%u seconds; weight ramp, orbit sweep, smoothing, blend, night ambient\n",maxStep,full,zero);
}
