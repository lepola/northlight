#include "weather_lens.h"
#include "weather_lens_heat.h"
#include <cassert>
#include <chrono>
#include <cstdio>

using namespace NorthlightWeatherLens;
static Drop drop(float x,float y,bool snow=false){Drop p;p.x=p.previousX=x;p.y=p.previousY=y;(snow?p.snow:p.water)=.0001f;p.age=1;p.life=100;p.seed=.1234f;return p;}
static double mass(const State& s){double total=0;for(unsigned i=0;i<s.count;++i)total+=s.drops[i].mass();return total;}
int main(){
    static_assert(sizeof(State)<16384&&sizeof(Vertex)*Capacity*6<80000);
    State merged;merged.primed=true;merged.count=2;merged.drops[0]=drop(.5f,.5f);merged.drops[1]=drop(.515f,.5f);
    const double original=mass(merged);merged.merge();assert(merged.count==1&&merged.merges==1);
    assert(std::abs(mass(merged)-original)<1e-10&&merged.drops[0].radius()>.014f);
    assert(std::abs(merged.drops[0].x-.5075f)<1e-6);
    State swept;swept.count=2;swept.drops[0]=drop(.5f,.4f);swept.drops[0].y=.6f;swept.drops[0].vy=.2f;swept.drops[1]=drop(.5f,.5f);
    swept.merge();assert(swept.count==1);std::array<Vertex,Capacity*6> vertices{};assert(swept.vertices(vertices.data(),1080)==6);
    assert(vertices[0].tail>.02f&&vertices[0].snow==0); // A fast collision forms a continuous rivulet.
    State clump;clump.count=2;clump.drops[0]=drop(.5f,.5f,true);clump.drops[1]=drop(.51f,.5f,true);clump.merge();
    assert(clump.count==1&&clump.drops[0].snow>.00019f);clump.vertices(vertices.data(),1080);assert(vertices[0].snow==1&&vertices[0].tail==0);
    State cold,hot;cold.primed=hot.primed=true;cold.count=hot.count=1;cold.drops[0]=hot.drops[0]=drop(.5f,.2f,true);
    for(unsigned f=0;f<=360;++f){cold.advance(float(f)/120,0,true,0,1);hot.advance(float(f)/120,0,true,1,1);}
    assert(cold.count==1&&hot.count==1&&cold.drops[0].snow>.000095f&&hot.drops[0].snow<.0000001f);
    assert(std::abs(mass(hot)-.0001)<1e-9&&hot.drops[0].water>.000099f);
    hot.vertices(vertices.data(),1080);assert(vertices[0].snow<.001f&&vertices[0].tail>0);
    float finalY=0;
    for(unsigned fps:{30u,60u,120u,240u}){State s;s.primed=true;s.count=1;s.drops[0]=drop(.5f,.1f);
        for(unsigned f=0;f<=fps;++f)s.advance(float(f)/fps,0,false,0,1);
        if(finalY)assert(std::abs(s.drops[0].y-finalY)<3e-5);finalY=s.drops[0].y;
    }
    State pattern;pattern.advance(0,1,false,0,16.f/9);assert(pattern.count>80);
    float mean=0,variance=0;for(unsigned i=0;i<pattern.count;++i)mean+=pattern.drops[i].x/pattern.count;
    for(unsigned i=0;i<pattern.count;++i)variance+=(pattern.drops[i].x-mean)*(pattern.drops[i].x-mean)/pattern.count;
    assert(variance>.1f);
    using L=NorthlightLocalLights::Light;std::array<L,128> lights{};float eye[3]={0,0,0};unsigned rays=0;
    auto clear=[&](const float*,const float*){++rays;return false;};
    lights[0]={{1,0,0},{1,.3f,.1f},0,8,1,3,NorthlightLocalLights::FirePulse};
    const float heat=NorthlightWeatherLensHeat::sample(lights,eye,nullptr,clear);assert(heat>.8f&&rays<=2);
    assert(NorthlightWeatherLensHeat::sample(lights,eye,nullptr,[](const float*,const float*){return true;})==0);
    lights[0].flags=0;assert(NorthlightWeatherLensHeat::sample(lights,eye,nullptr,clear)==0); // An orange window is not a fire.
    for(unsigned i=0;i<128;++i)lights[i]={{1+float(i)*.01f,0,0},{1,.3f,.1f},0,8,1+i,3,NorthlightLocalLights::FirePulse};
    rays=0;NorthlightWeatherLensHeat::sample(lights,eye,eye,clear);assert(rays<=2);
    State stress;auto start=std::chrono::steady_clock::now();
    for(unsigned f=0;f<12000;++f){stress.advance(float(f)/120,1,f%2400<1200,f%2400>1000?1.f:0.f,16.f/9);assert(stress.count<=Capacity);
        const unsigned n=stress.vertices(vertices.data(),1080);assert(n==stress.count*6);
        for(unsigned i=0;i<stress.count;++i){const auto& p=stress.drops[i];assert(std::isfinite(p.x+p.y+p.water+p.snow+p.vy)&&p.mass()>0&&p.water>=0&&p.snow>=0);}
    }
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/12000;
    std::printf("PASS lens masses: merge/sweep conservation, rivulets, snow clumps, fire-to-water, blocked heat, 30..240 FPS, random positions, cap=%u merges=%u stateBytes=%zu vertexBytes=%zu meanCpuUs=%.3f\n",Capacity,stress.merges,sizeof(State),sizeof(vertices),us);
}
