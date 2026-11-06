#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "weather_lens_clock.h"
#include "weather_motion.h"

// Bounded lens-space water/snow masses. No heap, GPU history or game particles.
namespace NorthlightWeatherLens {
constexpr unsigned Capacity=192;
constexpr unsigned MergeCapacity=32,MaxVertices=(Capacity+MergeCapacity)*6;
struct Drop {
    float x=0,y=0,water=0,snow=0,vx=0,vy=0,age=0,life=8,seed=0,joins=0,previousX=0,previousY=0,runLength=0;
    float visualX=0,visualY=0,visualRadius=0,visualSnow=0,shape=0,shapeTarget=0;
    float mass()const{return water+snow;}
    float radius()const{return std::sqrt(std::max(mass(),0.f));}
    float tailForSpeed(float speed)const{return water/std::max(mass(),1e-12f)*std::min(.30f,std::max(runLength,speed*(.22f+.24f*joins)));}
    float tail()const{return tailForSpeed(std::sqrt(vx*vx+vy*vy));}
    float angle()const{return std::atan2(vx,vy);}
    float bend(float tailLength)const{return std::sin(seed*19.f)*tailLength*.12f;}
    void primeVisual(){if(visualRadius>0)return;visualX=x;visualY=y;visualRadius=radius();visualSnow=snow/std::max(mass(),1e-12f);shape=shapeTarget=seed*16;}
};
struct Vertex {float x,y,z,w,u,v,dx,dy,radius,tail,bend,snow,opacity,seed,shape,angle;};
static_assert(sizeof(Vertex)==64,"Lens FVF layout");
struct State {
    std::array<Drop,Capacity> drops{};
    struct MergeFade {Drop drop;float age=0,targetX=0,targetY=0;};
    std::array<MergeFade,MergeCapacity> mergeFades{};
    unsigned fadeCount=0;
    unsigned count=0,merges=0;
    uint32_t random=0x9e3779b9;
    NorthlightWeatherLensClock clock;
    float aspect=1,spawnDebt=0,size=1,frost=0;
    bool primed=false;
    float noise(){random^=random<<13;random^=random>>17;random^=random<<5;return float(random>>8)*(1.f/16777216);}
    void spawn(bool snowing){
        if(count==Capacity)return;
        auto& p=drops[count++];p={};p.x=noise()*aspect;p.y=noise();p.seed=noise();
        p.previousX=p.x;p.previousY=p.y;
        const float radius=(snowing?.0038f+.005f*noise():.0028f+.0037f*noise())*size;
        (snowing?p.snow:p.water)=radius*radius;p.vx=(noise()-.5f)*.006f;
        p.life=snowing?12+14*noise():5+7*noise();p.age=.3f+noise()*p.life*.35f;
        p.primeVisual();
    }
    void merge(bool rivulets=true){
        if(count<2){if(count)drops[0].primeVisual();return;}
        // Pairwise work is bounded by the 192-bead limit.
        std::array<float,Capacity> radii,tails,bends,axisX,axisY;
        auto measure=[&](unsigned i){const auto& p=drops[i];radii[i]=p.radius();
            if(!rivulets){tails[i]=0;return;}
            const float speed=std::sqrt(p.vx*p.vx+p.vy*p.vy);tails[i]=p.tailForSpeed(speed);bends[i]=p.bend(tails[i]);
            axisX[i]=speed>1e-6f?p.vx/speed:0;axisY[i]=speed>1e-6f?p.vy/speed:1;};
        for(unsigned i=0;i<count;++i){drops[i].primeVisual();measure(i);}
        auto inTrail=[&](unsigned head,unsigned bead){
            if(tails[head]<=0)return false;
            const auto& a=drops[head];const auto& b=drops[bead];const float dx=b.x-a.x,dy=b.y-a.y;
            const float up=-dx*axisX[head]-dy*axisY[head];
            if(up<0||up>tails[head]+radii[bead])return false;
            const float cross=dx*axisY[head]-dy*axisX[head];
            if(std::abs(cross)>radii[head]+radii[bead]+std::abs(bends[head]))return false;
            const float t=std::clamp(up/tails[head],0.f,1.f);
            const float width=radii[head]*(.40f-.25f*t);
            return std::abs(cross-bends[head]*t*t)<width+radii[bead];
        };
        for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;){
            auto& a=drops[i];const auto& b=drops[j];const float dx=a.previousX-b.previousX,dy=a.previousY-b.previousY,r=radii[i]+radii[j];
            const float vx=a.x-b.x-dx,vy=a.y-b.y-dy;
            const float endX=dx+vx,endY=dy+vy;
            const bool separated=(dx>r&&endX>r)||(dx<-r&&endX<-r)||(dy>r&&endY>r)||(dy<-r&&endY<-r);
            bool headHit=false;
            if(!separated){const float v2=vx*vx+vy*vy;
                const float t=v2>1e-12f?std::clamp(-(dx*vx+dy*vy)/v2,0.f,1.f):0;
                const float cx=dx+t*vx,cy=dy+t*vy;headHit=cx*cx+cy*cy<r*r;}
            const bool trailA=!headHit&&inTrail(i,j),trailB=!headHit&&!trailA&&inTrail(j,i);
            if(!headHit&&!trailA&&!trailB){++j;continue;}
            const float ma=a.mass(),mb=b.mass(),total=ma+mb;
            if(!(total>0)){++j;continue;}
            // A running head collects beads along its wet path without jumping backwards.
            if(headHit){a.x=(a.x*ma+b.x*mb)/total;a.y=(a.y*ma+b.y*mb)/total;}
            else if(trailB){a.x=b.x;a.y=b.y;}
            if(b.snow>0&&fadeCount<MergeCapacity)mergeFades[fadeCount++]={b,0,a.x,a.y};
            if(a.snow+b.snow>0)a.shapeTarget+=.5f+.7f*mb/total;
            a.previousX=a.x;a.previousY=a.y;
            a.vx=(a.vx*ma+b.vx*mb)/total;a.vy=(a.vy*ma+b.vy*mb)/total;
            a.water+=b.water;a.snow+=b.snow;a.age=std::min(a.age,b.age);
            a.life=std::max(a.life,b.life);a.joins=std::min(4.f,a.joins+b.joins+1);
            a.runLength=std::max(a.runLength,b.runLength);
            measure(i);
            drops[j]=drops[--count];radii[j]=radii[count];tails[j]=tails[count];
            if(rivulets){bends[j]=bends[count];axisX[j]=axisX[count];axisY[j]=axisY[count];}++merges;
        }
    }
    void advance(double seconds,float intensity,bool snowing,float heat,float imageAspect,float sizeScale=1.f,bool rivulets=true,const NorthlightWeatherMotion::Flow& forces={}){
        if(!std::isfinite(seconds)||seconds<0)return;
        if(!std::isfinite(imageAspect)||imageAspect<.25f||imageAspect>8)return;
        intensity=std::isfinite(intensity)?std::clamp(intensity,0.f,1.f):0;
        heat=std::isfinite(heat)?std::clamp(heat,0.f,1.f):0;
        sizeScale=std::isfinite(sizeScale)?std::clamp(sizeScale,.25f,3.f):1.f;
        if(size!=sizeScale){const float scale=sizeScale/size,gain=scale*scale;
            for(unsigned i=0;i<count;++i){drops[i].water*=gain;drops[i].snow*=gain;drops[i].visualRadius*=scale;}
            for(unsigned i=0;i<fadeCount;++i)mergeFades[i].drop.visualRadius*=scale;size=sizeScale;}
        if(aspect!=imageAspect){const float gain=imageAspect/aspect;
            for(unsigned i=0;i<count;++i){drops[i].x*=gain;drops[i].visualX*=gain;}
            for(unsigned i=0;i<fadeCount;++i){mergeFades[i].drop.visualX*=gain;mergeFades[i].targetX*=gain;}
            aspect=imageAspect;}
        if(clock.started&&seconds==clock.previous)return;
        const double before=clock.elapsed,oldTime=clock.previous;const bool started=clock.started;
        clock.advance(seconds,intensity);
        const float motion=float(clock.elapsed-before);
        const float dt=started?float(std::clamp(seconds-oldTime,0.,.1)):0;
        if(!primed){for(unsigned i=0,n=35+unsigned(80*intensity);i<n;++i)spawn(snowing);primed=true;}
        // Melting transfers mass, so the same flake becomes a wet bead and runs.
        const float thaw=1-std::exp(-dt*(.015f+heat*3.f));
        const float relax=1-std::exp(-dt*5),visualEase=1-std::exp(-dt*9),speedGain=dt>0?motion/dt:0;
        for(unsigned i=0;i<fadeCount;){mergeFades[i].age+=dt;if(mergeFades[i].age>=.35f)mergeFades[i]=mergeFades[--fadeCount];else ++i;}
        for(unsigned i=0;i<count;){
            auto& p=drops[i];p.primeVisual();p.age+=dt;const float melted=p.snow*thaw;p.snow-=melted;p.water+=melted;
            const float fraction=p.snow/std::max(p.mass(),1e-12f),radius=p.radius();
            const float waterSpeed=.026f+std::min(.17f,radius*9.f);
            const float drift=.025f*fraction+waterSpeed*(1-fraction);
            const float gust=1+.22f*std::sin(float(before+motion*.5f)*.47f+p.seed*2);
            const float target=drift*forces.y*gust;
            const float targetX=drift*forces.x*gust+std::sin(float(clock.elapsed)*1.3f+p.seed*19.f)*.009f*fraction;
            p.vx+=forces.shiftX*.8f;p.vy+=forces.shiftY*.8f;
            p.previousX=p.x;p.previousY=p.y;
            p.x+=(targetX*dt+(p.vx-targetX)*relax*.2f)*speedGain;p.vx+=(targetX-p.vx)*relax;
            p.y+=(target*dt+(p.vy-target)*relax*.2f)*speedGain;p.vy+=(target-p.vy)*relax;
            p.visualX+=p.x-p.previousX;p.visualY+=p.y-p.previousY;
            p.visualX+=(p.x-p.visualX)*visualEase;p.visualY+=(p.y-p.visualY)*visualEase;
            p.visualRadius+=(radius-p.visualRadius)*visualEase;p.visualSnow+=(fraction-p.visualSnow)*visualEase;
            p.shapeTarget+=motion*.07f*(.65f+p.seed);p.shape+=(p.shapeTarget-p.shape)*visualEase;
            if(rivulets&&p.joins>0)p.runLength=std::min(.30f,p.runLength+std::hypot(p.x-p.previousX,p.y-p.previousY));
            const float tail=rivulets?p.tail():0;
            if(p.y-radius-tail>1||p.x+radius<0||p.x-radius>aspect||p.age>=p.life){drops[i]=drops[--count];continue;}++i;
        }
        spawnDebt+=dt*(snowing?22:85)*intensity*(.45f+.55f*std::clamp(-forces.z,0.f,1.f));
        for(unsigned n=0;n<12&&spawnDebt>=1;++n){spawn(snowing);spawnDebt-=1;}
        spawnDebt=std::min(spawnDebt,1.f);if(snowing||rivulets)merge(rivulets);
        float snowMass=0;if(snowing)for(unsigned i=0;i<count;++i)if(drops[i].y<1)snowMass+=drops[i].snow;
        const float targetFrost=snowing?std::clamp(snowMass/(aspect*.0035f),0.f,1.f)*(.15f+.85f*intensity):0;
        const float frostRate=targetFrost>frost?.8f:.5f+heat*3.f;
        frost+=(targetFrost-frost)*(1-std::exp(-dt*frostRate));
    }
    unsigned vertices(Vertex* output,unsigned height,bool rivulets=true)const{
        unsigned used=0;
        auto emit=[&](const Drop& p,float x,float y,float r,float fraction,float opacity){
            r=std::clamp(r,.001f,.05f);
            const float tail=rivulets?p.tail():0;
            const float bend=p.bend(tail),angle=p.angle(),tx=-std::sin(angle)*tail,ty=-std::cos(angle)*tail;
            const float extent=r*(fraction>.01f?3.8f:1.15f)+std::abs(bend);
            const float left=x+std::min(0.f,tx)-extent,right=x+std::max(0.f,tx)+extent,top=y+std::min(0.f,ty)-extent,bottom=y+std::max(0.f,ty)+extent;
            const float screenX[2]={left*height-.5f,right*height-.5f},screenY[2]={top*height-.5f,bottom*height-.5f};
            const float texU[2]={left/aspect,right/aspect},ys[2]={top,bottom};
            const float deltaX[2]={left-x,right-x},deltaY[2]={top-y,bottom-y};
            const unsigned corners[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
            for(const auto& c:corners)output[used++]={screenX[c[0]],screenY[c[1]],0,1,texU[c[0]],ys[c[1]],deltaX[c[0]],deltaY[c[1]],r,tail,bend,fraction,opacity,p.seed,p.shape,angle};
        };
        for(unsigned i=0;i<count;++i){const auto& p=drops[i];
            const float opacity=std::min(1.f,p.age*4)*std::clamp((p.life-p.age)*1.5f,0.f,1.f);
            emit(p,p.visualRadius>0?p.visualX:p.x,p.visualRadius>0?p.visualY:p.y,p.visualRadius>0?p.visualRadius:p.radius(),p.visualRadius>0?p.visualSnow:p.snow/std::max(p.mass(),1e-12f),opacity);
        }
        for(unsigned i=0;i<fadeCount;++i){const auto& f=mergeFades[i];const float t=std::clamp(f.age/.35f,0.f,1.f),ease=t*t*(3-2*t);
            const float opacity=std::min(1.f,f.drop.age*4)*std::clamp((f.drop.life-f.drop.age)*1.5f,0.f,1.f);
            emit(f.drop,f.drop.visualX+(f.targetX-f.drop.visualX)*ease,f.drop.visualY+(f.targetY-f.drop.visualY)*ease,f.drop.visualRadius,f.drop.visualSnow,(1-ease)*opacity);}
        return used;
    }
};
}
