#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "weather_lens_clock.h"

// Bounded lens-space water/snow masses. No heap, GPU history or game particles.
namespace NorthlightWeatherLens {
constexpr unsigned Capacity=192;
struct Drop {
    float x=0,y=0,water=0,snow=0,vx=0,vy=0,age=0,life=8,seed=0,joins=0,previousX=0,previousY=0;
    float mass()const{return water+snow;}
    float radius()const{return std::sqrt(std::max(mass(),0.f));}
};
struct Vertex {float x,y,z,w,u,v,dx,dy,radius,tail,bend,snow,opacity,seed,reserved0,reserved1;};
static_assert(sizeof(Vertex)==64,"Lens FVF layout");
struct State {
    std::array<Drop,Capacity> drops{};
    unsigned count=0,merges=0;
    uint32_t random=0x9e3779b9;
    NorthlightWeatherLensClock clock;
    float aspect=1,spawnDebt=0;
    bool primed=false;
    float noise(){random^=random<<13;random^=random>>17;random^=random<<5;return float(random>>8)*(1.f/16777216);}
    void spawn(bool snowing){
        if(count==Capacity)return;
        auto& p=drops[count++];p={};p.x=noise()*aspect;p.y=noise();p.seed=noise();
        p.previousX=p.x;p.previousY=p.y;
        const float radius=snowing?.006f+.007f*noise():.0028f+.0037f*noise();
        (snowing?p.snow:p.water)=radius*radius;p.vx=(noise()-.5f)*.006f;
        p.life=snowing?12+14*noise():5+7*noise();p.age=.3f+noise()*p.life*.35f;
    }
    void merge(){
        // Pairwise work is bounded by the 192-bead limit.
        std::array<float,Capacity> radii{};for(unsigned i=0;i<count;++i)radii[i]=drops[i].radius();
        for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;){
            auto& a=drops[i];const auto& b=drops[j];const float dx=a.previousX-b.previousX,dy=a.previousY-b.previousY,r=radii[i]+radii[j];
            const float vx=a.x-b.x-dx,vy=a.y-b.y-dy,v2=vx*vx+vy*vy;
            const float endX=dx+vx,endY=dy+vy;
            if((dx>r&&endX>r)||(dx<-r&&endX<-r)||(dy>r&&endY>r)||(dy<-r&&endY<-r)){++j;continue;}
            const float t=v2>1e-12f?std::clamp(-(dx*vx+dy*vy)/v2,0.f,1.f):0;
            const float cx=dx+t*vx,cy=dy+t*vy;
            if(cx*cx+cy*cy>=r*r){++j;continue;}
            const float ma=a.mass(),mb=b.mass(),total=ma+mb;
            if(!(total>0)){++j;continue;}
            a.x=(a.x*ma+b.x*mb)/total;a.y=(a.y*ma+b.y*mb)/total;
            a.previousX=a.x;a.previousY=a.y;
            a.vx=(a.vx*ma+b.vx*mb)/total;a.vy=(a.vy*ma+b.vy*mb)/total;
            a.water+=b.water;a.snow+=b.snow;a.age=std::min(a.age,b.age);
            a.life=std::max(a.life,b.life);a.joins=std::min(4.f,a.joins+b.joins+1);
            radii[i]=std::sqrt(total);drops[j]=drops[--count];radii[j]=radii[count];++merges;
        }
    }
    void advance(double seconds,float intensity,bool snowing,float heat,float imageAspect){
        if(!std::isfinite(seconds)||seconds<0)return;
        if(!std::isfinite(imageAspect)||imageAspect<.25f||imageAspect>8)return;
        intensity=std::isfinite(intensity)?std::clamp(intensity,0.f,1.f):0;
        heat=std::isfinite(heat)?std::clamp(heat,0.f,1.f):0;
        if(aspect!=imageAspect){for(unsigned i=0;i<count;++i)drops[i].x*=imageAspect/aspect;aspect=imageAspect;}
        const double before=clock.elapsed,oldTime=clock.previous;const bool started=clock.started;
        clock.advance(seconds,intensity);
        const float motion=float(clock.elapsed-before);
        const float dt=started?float(std::clamp(seconds-oldTime,0.,.1)):0;
        if(!primed){for(unsigned i=0,n=35+unsigned(80*intensity);i<n;++i)spawn(snowing);primed=true;}
        // Melting transfers mass, so the same flake becomes a wet bead and runs.
        const float thaw=1-std::exp(-dt*(.015f+heat*3.f));
        const float relax=1-std::exp(-dt*5),speedGain=dt>0?motion/dt:0;
        for(unsigned i=0;i<count;){
            auto& p=drops[i];p.age+=dt;const float melted=p.snow*thaw;p.snow-=melted;p.water+=melted;
            const float fraction=p.snow/std::max(p.mass(),1e-12f),radius=p.radius();
            const float waterSpeed=.026f+std::min(.17f,radius*9.f);
            const float target=.004f*fraction+waterSpeed*(1-fraction);
            p.previousX=p.x;p.previousY=p.y;p.x+=p.vx*motion;
            p.y+=(target*dt+(p.vy-target)*relax*.2f)*speedGain;p.vy+=(target-p.vy)*relax;
            if(p.y-radius>1||p.x+radius<0||p.x-radius>aspect||p.age>=p.life){drops[i]=drops[--count];continue;}++i;
        }
        spawnDebt+=dt*(snowing?22:85)*intensity;
        for(unsigned n=0;n<12&&spawnDebt>=1;++n){spawn(snowing);spawnDebt-=1;}
        spawnDebt=std::min(spawnDebt,1.f);merge();
    }
    unsigned vertices(Vertex* output,unsigned height)const{
        unsigned used=0;
        for(unsigned i=0;i<count;++i){const auto& p=drops[i];const float r=std::clamp(p.radius(),.001f,.05f),fraction=p.snow/std::max(p.mass(),1e-12f);
            const float tail=(1-fraction)*std::min(.22f,p.vy*(.16f+.14f*p.joins));
            const float bend=p.vx*(tail/std::max(p.vy,.001f));
            const float opacity=std::min(1.f,p.age*4)*std::clamp((p.life-p.age)*1.5f,0.f,1.f);
            const float extent=r*2.f,left=p.x-extent-std::abs(bend),right=p.x+extent+std::abs(bend),top=p.y-extent-tail,bottom=p.y+extent;
            const float corners[6][2]={{left,top},{right,top},{left,bottom},{left,bottom},{right,top},{right,bottom}};
            for(const auto& c:corners)output[used++]={c[0]*height-.5f,c[1]*height-.5f,0,1,c[0]/aspect,c[1],c[0]-p.x,c[1]-p.y,r,tail,bend,fraction,opacity,p.seed,0,0};
        }
        return used;
    }
};
}
