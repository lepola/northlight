#include "weather_lens.h"
#include "weather_sand_lens.h"
#include "weather_lens_heat.h"
#include <cassert>
#include <chrono>
#include <cstdio>

using namespace NorthlightWeatherLens;
static Drop drop(float x,float y,bool snow=false){Drop p;p.x=p.previousX=x;p.y=p.previousY=y;(snow?p.snow:p.water)=.0001f;p.age=1;p.life=100;p.seed=.1234f;return p;}
static double mass(const State& s){double total=0;for(unsigned i=0;i<s.count;++i)total+=s.drops[i].mass();return total;}
int main(){
    static_assert(sizeof(State)<20480&&sizeof(Vertex)*MaxVertices<96000);
    State merged;merged.primed=true;merged.count=2;merged.drops[0]=drop(.5f,.5f);merged.drops[1]=drop(.515f,.5f);
    const double original=mass(merged);merged.merge();assert(merged.count==1&&merged.merges==1);
    assert(std::abs(mass(merged)-original)<1e-10&&merged.drops[0].radius()>.014f);
    assert(std::abs(merged.drops[0].x-.5075f)<1e-6);
    State trail;trail.primed=true;trail.count=2;trail.drops[0]=drop(.5f,.7f);trail.drops[1]=drop(.5f,.6f);
    trail.drops[0].vy=.2f;trail.drops[0].joins=2;trail.drops[0].runLength=.16f;trail.drops[0].seed=0;
    const double trailMass=mass(trail);trail.merge();assert(trail.count==1&&trail.merges==1);
    assert(std::abs(mass(trail)-trailMass)<1e-10&&trail.drops[0].y==.7f); // Wet path captures a bead without head collision or teleporting.
    State horizontal;horizontal.count=2;horizontal.drops[0]=drop(.7f,.5f);horizontal.drops[1]=drop(.6f,.5f);
    horizontal.drops[0].vx=.2f;horizontal.drops[0].joins=2;horizontal.drops[0].runLength=.16f;horizontal.drops[0].seed=0;
    horizontal.merge();assert(horizontal.count==1&&horizontal.drops[0].x==.7f);
    State independent;independent.primed=true;independent.count=2;independent.drops[0]=drop(.5f,.5f);independent.drops[1]=drop(.505f,.5f);
    independent.advance(0,0,false,0,1,1,false);assert(independent.count==2&&independent.merges==0);
    std::array<Vertex,MaxVertices> isolated{};independent.vertices(isolated.data(),1080,false);for(unsigned i=0;i<12;++i)assert(isolated[i].tail==0);
    independent.advance(1.f/60,0,false,0,1,1,true);assert(independent.count==1&&independent.merges==1);
    State paused;paused.primed=true;paused.count=3;paused.drops[0]=drop(.4f,.5f);paused.drops[1]=drop(.427f,.5f);paused.drops[2]=drop(.415f,.5f);
    paused.advance(0,0,false,0,1);assert(paused.count==2);
    paused.advance(0,0,false,0,1);assert(paused.count==2); // A paused frame must not continue a merge cascade.
    paused.advance(1.f/60,0,false,0,1);assert(paused.count==1);
    State swept;swept.count=2;swept.drops[0]=drop(.5f,.4f);swept.drops[0].y=.6f;swept.drops[0].vy=.2f;swept.drops[1]=drop(.5f,.5f);
    swept.merge();assert(swept.count==1);std::array<Vertex,MaxVertices> vertices{};assert(swept.vertices(vertices.data(),1080)==6);
    assert(vertices[0].tail>.02f&&vertices[0].snow==0); // A fast collision forms a continuous rivulet.
    State clump;clump.count=2;clump.drops[0]=drop(.5f,.5f,true);clump.drops[1]=drop(.51f,.5f,true);clump.merge();
    assert(clump.count==1&&clump.drops[0].snow>.00019f);clump.vertices(vertices.data(),1080);assert(vertices[0].snow==1&&vertices[0].tail==0);
    assert(clump.fadeCount==1&&clump.vertices(vertices.data(),1080)==12);
    assert(std::abs(clump.drops[0].visualRadius-.01f)<1e-6&&clump.drops[0].visualX==.5f);
    const float shapeBefore=clump.drops[0].shape;clump.primed=true;
    clump.advance(0,0,true,0,1);clump.advance(.016,0,true,0,1);
    assert(clump.drops[0].visualRadius>.01f&&clump.drops[0].visualRadius<clump.drops[0].radius());
    assert(clump.drops[0].shape>shapeBefore&&clump.drops[0].shape<clump.drops[0].shapeTarget);
    for(unsigned frame=2;frame<=60;++frame)clump.advance(frame/60.,0,true,0,1);
    assert(clump.fadeCount==0&&std::abs(clump.drops[0].visualRadius-clump.drops[0].radius())<1e-5);
    State faint;faint.count=2;faint.drops[0]=drop(.5f,.5f,true);faint.drops[1]=drop(.51f,.5f,true);faint.drops[1].age=99.99f;
    faint.merge();faint.vertices(vertices.data(),1080);assert(vertices[6].opacity<.02f); // An absorbed fading flake must not flash back to full opacity.
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
    for(bool snow:{false,true}){State drift;drift.primed=true;drift.count=1;drift.drops[0]=drop(.3f,.2f,snow);
        NorthlightWeatherMotion::Flow wind;wind.x=1;
        for(unsigned f=0;f<=120;++f)drift.advance(f/60.,0,snow,0,1,1,true,wind);
        assert(drift.drops[0].x>(snow?.305f:.32f)&&std::abs(drift.drops[0].vx)>.01f);
        drift.vertices(vertices.data(),1080);assert(std::abs(vertices[0].angle)>.05f);
        const float x=drift.drops[0].x;drift.advance(2,0,snow,0,1);assert(drift.drops[0].x==x);
    }
    State pattern;pattern.advance(0,1,false,0,16.f/9);assert(pattern.count>80);
    // Live size tuning rescales both phases of an existing mass and future spawns.
    State sized;sized.primed=true;sized.count=1;sized.drops[0]=drop(.5f,.2f,true);
    sized.advance(0,0,true,0,1,2);assert(std::abs(sized.drops[0].radius()-.02f)<1e-6f);
    sized.advance(0,0,true,0,1,1);assert(std::abs(sized.drops[0].radius()-.01f)<1e-6f);
    State small,large;small.advance(0,.5f,false,0,1,.5f);large.advance(0,.5f,false,0,1,2);
    assert(std::abs(mass(large)/mass(small)-16)<1e-4); // Larger collision radii may already merge seeded drops.
    float mean=0,variance=0;for(unsigned i=0;i<pattern.count;++i)mean+=pattern.drops[i].x/pattern.count;
    for(unsigned i=0;i<pattern.count;++i)variance+=(pattern.drops[i].x-mean)*(pattern.drops[i].x-mean)/pattern.count;
    assert(variance>.1f);
    State drizzle,blizzard;
    for(unsigned f=0;f<=360;++f){drizzle.advance(float(f)/60,.1f,true,0,16.f/9);blizzard.advance(float(f)/60,1,true,0,16.f/9);}
    assert(blizzard.frost>.15f&&blizzard.frost>drizzle.frost*2);
    const float frozenEdge=blizzard.frost;
    for(unsigned f=1;f<=180;++f)blizzard.advance(6+float(f)/60,0,true,1,16.f/9);
    assert(blizzard.frost<frozenEdge*.3f); // Fire melts the accumulated edge as well as individual flakes.
    using L=NorthlightLocalLights::Light;std::array<L,128> lights{};float eye[3]={0,0,0};unsigned rays=0;
    {NorthlightSurfaceFrame view{};view.c[1][0]=1.3f;view.c[1][1]=1.95f;view.c[1][2]=-1;view.c[4][1]=1;view.c[5][2]=1;view.c[6][0]=-1;
     NorthlightWeatherMotion::Tracker wind;wind.update(view,1,90,100,true,0);assert(wind.flow.x>.39f&&std::abs(wind.flow.z)<1e-5f&&wind.flow.y==1);
     wind.update(view,1,90,100,true,.1);wind.update(view,1,270,100,true,.1);assert(wind.flow.shiftX==0);
     const float angle=.15f;view.c[4][0]=-std::sin(angle);view.c[4][1]=std::cos(angle);view.c[6][0]=-std::cos(angle);view.c[6][1]=-std::sin(angle);
     wind.update(view,1,90,100,true,.116);assert(wind.flow.shiftX<0&&wind.flow.z>0);
     wind.update(view,1,90,100,false,.132);assert(wind.flow.shiftX==0&&wind.flow.shiftY==0);
     view.c[6][0]=1;view.c[6][1]=0;view.c[4][0]=0;view.c[4][1]=-1;wind.update(view,1,90,100,true,.148);assert(wind.flow.shiftX==0); // Camera cuts do not create an impulse.
     for(float direction:{-1.f,1.f}){State sideways;sideways.primed=true;sideways.count=1;sideways.drops[0]=drop(.5f,.5f);NorthlightWeatherMotion::Flow f;f.x=direction;f.y=0;
      for(unsigned i=0;i<=60;++i)sideways.advance(i/60.,0,false,0,1,1,true,f);assert((sideways.drops[0].x-.5f)*direction>.02f);assert(std::abs(sideways.drops[0].angle())>1.4f);}}
    auto clear=[&](const float*,const float*){++rays;return false;};
    lights[0]={{1,0,0},{1,.3f,.1f},0,8,1,3,NorthlightLocalLights::FirePulse};
    const float heat=NorthlightWeatherLensHeat::sample(lights,eye,nullptr,clear);assert(heat>.8f&&rays<=2);
    assert(NorthlightWeatherLensHeat::sample(lights,eye,nullptr,[](const float*,const float*){return true;})==0);
    lights[0].flags=0;assert(NorthlightWeatherLensHeat::sample(lights,eye,nullptr,clear)==0); // An orange window is not a fire.
    for(unsigned i=0;i<128;++i)lights[i]={{1+float(i)*.01f,0,0},{1,.3f,.1f},0,8,1+i,3,NorthlightLocalLights::FirePulse};
    rays=0;NorthlightWeatherLensHeat::sample(lights,eye,eye,clear);assert(rays<=2);
    State stress;auto start=std::chrono::steady_clock::now();
    {NorthlightWeatherMotion::Flow front,back;front.z=-1;back.z=1;NorthlightWeatherSand::State incoming,outgoing;
     incoming.advance(0,1,1,1,front);outgoing.advance(0,1,1,1,back);assert(incoming.count>outgoing.count&&incoming.count<=NorthlightWeatherSand::Capacity);
     const float radius=incoming.specks[0].radius,x=incoming.specks[0].x;incoming.advance(0,1,1,2,back);assert(incoming.specks[0].radius==radius*2&&incoming.specks[0].x==x);
     for(float direction:{-1.f,1.f}){NorthlightWeatherSand::State sand;sand.primed=true;sand.count=1;sand.specks[0]={.5f,.5f,0,0,.004f,1,100,.9f};NorthlightWeatherMotion::Flow f;f.x=direction;f.y=0;
      for(unsigned i=0;i<=60;++i)sand.advance(i/60.,0,1,1,f);assert((sand.specks[0].x-.5f)*direction>.01f);}}
    for(unsigned f=0;f<12000;++f){stress.advance(float(f)/120,1,f%2400<1200,f%2400>1000?1.f:0.f,16.f/9);assert(stress.count<=Capacity);
        const unsigned n=stress.vertices(vertices.data(),1080);assert(n==(stress.count+stress.fadeCount)*6&&n<=MaxVertices);
        for(unsigned i=0;i<stress.count;++i){const auto& p=stress.drops[i];assert(std::isfinite(p.x+p.y+p.water+p.snow+p.vy)&&p.mass()>0&&p.water>=0&&p.snow>=0);}
    }
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/12000;
    std::printf("PASS lens masses: merge/sweep conservation, rivulets, snow clumps, fire-to-water, blocked heat, 30..240 FPS, random positions, cap=%u merges=%u stateBytes=%zu vertexBytes=%zu meanCpuUs=%.3f\n",Capacity,stress.merges,sizeof(State),sizeof(vertices),us);
}
