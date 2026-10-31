#include "spell_fog_march.h"
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <random>

using namespace NorthlightSpellFog;

int main(){
    using namespace NorthlightRegionalFog;
    Field field;field.originX=field.originY=-256;
    for(unsigned i=0;i<N*N;++i)field.texels[i]={float(int(i%23)-10),.02f,.01f,i%7?5.f:0.f};
    March march;march.night=.5f;march.volume[0]=.02f;march.volume[1]=.01f;march.volume[2]=60;march.volume[3]=30;
    float from[3]={0,0,4};
    auto volume=NorthlightFogClouds::generate(42);
    auto quantiles=NorthlightFogClouds::quantiles(volume.data());
    NorthlightFogClouds::Wind wind;
    auto clouds=NorthlightFogClouds::derive(1,100,.5,.5,wind,from,&quantiles);
    auto extra=[&](float x,float y,float z,const FieldSample& sample){
        const float point[]={x,y,z};
        return NorthlightFogClouds::sigmaAt(volume.data(),clouds,from,point,sample.ground,sample.height,sample.coverage);
    };
    auto none=[](float,float,float,const FieldSample&){return 0.f;};
    std::mt19937 rng(104);
    for(int i=0;i<10000;++i){
        float to[]={float(int(rng()%500)-250),float(int(rng()%500)-250),float(int(rng()%100)-50)};
        const unsigned steps=rng()%60;
        const float clear=opticalDepth(field,march,from,to,steps,none);
        const float cloudy=opticalDepth(field,march,from,to,steps,extra);
        float pairedClear,pairedCloudy;
        opticalDepthPair(field,march,from,to,steps,extra,pairedClear,pairedCloudy);
        assert(!std::memcmp(&clear,&pairedClear,4)&&!std::memcmp(&cloudy,&pairedCloudy,4));
    }
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!cloudRayAboveSupport({0,nan,1},12,200,200));
    assert(!cloudRayAboveSupport({0,50,0},12,200,200));
    assert(!cloudRayAboveSupport({0,50,1},12,200,100));
    assert(cloudRayAboveSupport({0,50,1},12,200,200));
    const auto bounds=globalHeightBounds(field);
    from[2]=200;const float highTo[]={40,30,190};
    assert(cloudRayAboveSupport(bounds,clouds.height,from[2],highTo[2]));
    float clear,cloudy;opticalDepthPair(field,march,from,highTo,24,extra,clear,cloudy);
    assert(!std::memcmp(&clear,&cloudy,4));
}
