#pragma once
#include "weather_lens.h"

// Dust on the lens: positions stay in glass coordinates, independent of scene geometry.
namespace NorthlightWeatherSand {
constexpr unsigned Capacity=128;
struct Speck {float x=0,y=0,vx=0,vy=0,radius=0,age=0,life=0,seed=0;};
struct State {
    std::array<Speck,Capacity> specks{};unsigned count=0;uint32_t random=0x51a7;
    NorthlightWeatherLensClock clock;float aspect=1,debt=0,sizeScale=1;bool primed=false;
    float noise(){random^=random<<13;random^=random>>17;random^=random<<5;return float(random>>8)*(1.f/16777216);}
    void spawn(float size,bool initial){
        if(count==Capacity)return;auto& p=specks[count++];p={};p.x=noise()*aspect;p.y=noise();p.seed=noise();
        p.radius=(p.seed<.15f?.003f+.009f*noise():.0008f+.0032f*noise())*size;p.life=3+8*noise();p.age=initial?noise()*p.life*.8f:0;
    }
    void advance(double time,float intensity,float imageAspect,float size,const NorthlightWeatherMotion::Flow& flow){
        if(!std::isfinite(time)||!std::isfinite(imageAspect)||imageAspect<=0)return;
        intensity=std::clamp(intensity,0.f,1.f);size=std::clamp(size,.25f,3.f);
        if(sizeScale!=size){for(unsigned i=0;i<count;++i)specks[i].radius*=size/sizeScale;sizeScale=size;}
        if(aspect!=imageAspect){for(unsigned i=0;i<count;++i)specks[i].x*=imageAspect/aspect;aspect=imageAspect;}
        if(clock.started&&time==clock.previous)return;
        const double before=clock.elapsed,previous=clock.previous;const bool started=clock.started;clock.advance(time,intensity);
        const float dt=started?float(std::clamp(time-previous,0.,.1)):0,travel=float(clock.elapsed-before),ease=1-std::exp(-dt*4);
        const float impact=.35f+.65f*std::clamp(-flow.z,0.f,1.f);
        if(!primed){for(unsigned i=0,n=unsigned(36+88*intensity*impact);i<n;++i)spawn(size,true);primed=true;}
        for(unsigned i=0;i<count;){auto& p=specks[i];p.age+=dt;
            const float adhesion=p.seed<.65f?.12f:1.f;
            p.vx+=flow.shiftX*.8f*adhesion;p.vy+=flow.shiftY*.8f*adhesion;
            const float tx=flow.x*(.035f+.06f*p.seed)*adhesion,ty=(flow.y*.035f+.008f)*adhesion;
            p.vx+=(tx-p.vx)*ease;p.vy+=(ty-p.vy)*ease;p.x+=p.vx*travel;p.y+=p.vy*travel;
            if(p.age>=p.life||p.x<-.02f||p.x>aspect+.02f||p.y<-.02f||p.y>1.02f){specks[i]=specks[--count];continue;}++i;
        }
        debt+=dt*(12+34*intensity)*impact;for(unsigned i=0;i<12&&debt>=1;++i){spawn(size,false);debt-=1;}
        debt=std::min(debt,1.f);
    }
    unsigned vertices(NorthlightWeatherLens::Vertex* out,unsigned height)const{
        unsigned used=0;
        for(unsigned i=0;i<count;++i){const auto& p=specks[i];const float r=p.radius,opacity=std::min(1.f,p.age*5)*std::clamp((p.life-p.age)*2,0.f,1.f);
            const float corners[6][2]={{-r,-r},{r,-r},{-r,r},{-r,r},{r,-r},{r,r}};
            for(const auto& c:corners){const float x=p.x+c[0],y=p.y+c[1];out[used++]={x*height-.5f,y*height-.5f,0,1,x/aspect,y,c[0],c[1],r,0,0,0,opacity,p.seed,0,0};}
        }return used;
    }
};
}
