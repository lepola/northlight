// 0.3.201 (task 17): native test of ui_boundary.h (fallback effect boundary learning). No D3D.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "ui_boundary.h"
using namespace NorthlightUiBoundary;
using FK=Arming::FrameKind;using R=Arming::Result;
static void ortho(float c[16],float w=1920,float h=1080){
    const float m[16]={2/w,0,0,-1, 0,-2/h,0,1, 0,0,1,0, 0,0,0,1};for(int i=0;i<16;++i)c[i]=m[i];
}
static void frames(Arming& a,FK k,unsigned n,unsigned drawsA=0,unsigned* armed=nullptr,unsigned* lost=nullptr){
    for(unsigned f=0;f<n;++f){
        for(unsigned i=0;i<drawsA;++i)a.noteCandidate(0xA,0x1);
        const R r=a.endFrame(k);if(r==R::Armed&&armed)++*armed;if(r==R::Lost&&lost)++*lost;
    }
}
int main(){
    float c[16];ortho(c);
    assert(clipWOne(c+12)&&screenSpaceRows(c));
    float p[16];ortho(p);p[14]=1;p[15]=0;assert(!clipWOne(p+12)&&!screenSpaceRows(p));
    ortho(p);p[5]=std::numeric_limits<float>::quiet_NaN();assert(!screenSpaceRows(p));
    ortho(p);p[1]=std::numeric_limits<float>::infinity();assert(!screenSpaceRows(p));
    for(float& v:p)v=0;assert(!screenSpaceRows(p));
    ortho(p);p[15]=1.01f;assert(!screenSpaceRows(p));
    ortho(p);for(int i=0;i<8;++i)p[i]=0;assert(!screenSpaceRows(p)); // rows 0 and 1 empty
    ortho(p);p[0]=0;p[3]=0;assert(screenSpaceRows(p)); // row 1 still carries a scale
    { // accepts
        const Candidate ok{true,false,false,false,true,c};assert(accepts(ok));
        Candidate d=ok;d.afterWorld=false;assert(!accepts(d));
        d=ok;d.worldVs=true;assert(!accepts(d));
        d=ok;d.waterVs=true;assert(!accepts(d));
        d=ok;d.zWrite=true;assert(!accepts(d));
        d=ok;d.fullTarget=false;assert(!accepts(d));
        d=ok;d.c=nullptr;assert(!accepts(d));
        d=ok;d.c=p;ortho(p);p[15]=0;assert(!accepts(d));
    }
    { // (a) stock: the hash boundary fires in frame 1
        Arming a;unsigned armed=0;assert(a.learning()&&!a.armed()&&!a.disarmed());
        assert(a.endFrame(FK::WorldHash)==R::None);assert(a.disarmed()&&!a.learning());
        for(unsigned f=0;f<1000;++f){for(int i=0;i<40;++i)a.noteCandidate(0xA,0x1);if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;}
        assert(armed==0&&a.disarmed()&&!a.armed());
    }
    { // (b) font mod: dominant pair plus a stray one
        Arming a;unsigned armed=0;
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            for(int i=0;i<40;++i)a.noteCandidate(0xA,0x1);a.noteCandidate(0xB,0x9);
            if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;
        }
        assert(armed==1&&a.armed()&&!a.learning()&&!a.disarmed());
        assert(a.isBoundary(0xA,0x1)&&!a.isBoundary(0xB,0x9)&&!a.isBoundary(0xA,0x9));
        assert(a.learnedVs()==0xA&&a.learnedPsCount()==1&&std::fabs(a.drawsPerFrame()-40.f)<.5f);
        frames(a,FK::WorldFallback,50,0,&armed);assert(armed==1&&a.armed());
    }
    { // (c) NoWorld never arms and resets the streak
        Arming a;unsigned armed=0;frames(a,FK::NoWorld,500,40,&armed);assert(armed==0&&a.learning());
        frames(a,FK::WorldMissed,119,40,&armed);assert(armed==0);
        frames(a,FK::NoWorld,1,40,&armed); // streak reset, counts cleared
        frames(a,FK::WorldMissed,119,40,&armed);assert(armed==0&&!a.armed());
        frames(a,FK::WorldMissed,1,40,&armed);assert(armed==1&&a.armed());
    }
    { // (d) low density does not arm; rising density does
        Arming a;unsigned armed=0;frames(a,FK::WorldMissed,Arming::kArmFrames,2,&armed);assert(armed==0&&a.learning()&&a.candidateStreak()==0);
        frames(a,FK::WorldMissed,Arming::kArmFrames,12,&armed);assert(armed==1&&a.isBoundary(0xA,0x1));
    }
    { // (e) lost after 120 missed frames, relearn on a new VS
        Arming a;unsigned armed=0,lost=0;frames(a,FK::WorldMissed,Arming::kArmFrames,40,&armed,&lost);assert(armed==1);
        frames(a,FK::WorldMissed,Arming::kLostFrames-1,0,&armed,&lost);assert(lost==0&&a.armed());
        frames(a,FK::WorldFallback,1,0,&armed,&lost);frames(a,FK::WorldMissed,Arming::kLostFrames-1,0,&armed,&lost);assert(lost==0&&a.armed()); // fallback resets the lost streak
        frames(a,FK::WorldMissed,1,0,&armed,&lost);assert(lost==1&&a.learning()&&!a.isBoundary(0xA,0x1));
        frames(a,FK::WorldMissed,500,0,&armed,&lost);assert(lost==1);
        for(unsigned f=0;f<Arming::kArmFrames;++f){for(int i=0;i<30;++i)a.noteCandidate(0xB,0x2);if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;}
        assert(armed==2&&a.isBoundary(0xB,0x2)&&!a.isBoundary(0xA,0x1));
    }
    { // (f) forget un-arms
        Arming a;unsigned armed=0;frames(a,FK::WorldMissed,Arming::kArmFrames,40,&armed);assert(a.armed());
        a.forget(0x77);assert(a.armed());a.forget(0xA);assert(a.learning());
        frames(a,FK::WorldMissed,Arming::kArmFrames,40,&armed);assert(a.armed()&&armed==2);
        a.forget(0x1);assert(a.learning()&&!a.isBoundary(0xA,0x1));
        a.noteCandidate(0xC,0x3);a.noteCandidate(0xD,0x4);a.forget(0x3);
        Arming::Pair top[4];assert(a.topCandidates(top,4)==1&&top[0].vs==0xD);
    }
    { // (g) Neutral neither advances nor resets
        Arming a;unsigned armed=0;frames(a,FK::WorldMissed,60,40,&armed);frames(a,FK::Neutral,500,40,&armed);assert(armed==0&&a.candidateStreak()==60);
        frames(a,FK::WorldMissed,59,40,&armed);assert(armed==0);frames(a,FK::WorldMissed,1,40,&armed);assert(armed==1);
    }
    { // (h) more than kSlots distinct pairs
        Arming a;unsigned armed=0;
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            for(int i=0;i<40;++i)a.noteCandidate(0xA,0x1);
            for(unsigned k=0;k<40;++k)a.noteCandidate(0x1000+k,0x2000+k);
            if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;
        }
        assert(armed==1&&a.droppedPairs()>0&&a.isBoundary(0xA,0x1));
        Arming::Pair top[3];assert(a.topCandidates(top,3)==0); // counts cleared after arming
    }
    { // several PS on one VS: only shares >= 10% join, at most 4
        Arming a;
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            for(int i=0;i<50;++i)a.noteCandidate(0xA,0x1);for(int i=0;i<20;++i)a.noteCandidate(0xA,0x2);a.noteCandidate(0xA,0x3);
            a.endFrame(FK::WorldMissed);
        }
        assert(a.armed()&&a.learnedPsCount()==2&&a.isBoundary(0xA,0x2)&&!a.isBoundary(0xA,0x3));
    }
    { // collecting(): no state reads before kWarmFrames missed world frames, never after a hash boundary, always while armed
        Arming a;assert(!a.collecting());
        a.endFrame(FK::WorldMissed);assert(!a.collecting());
        a.endFrame(FK::WorldMissed);assert(a.collecting());
        a.endFrame(FK::NoWorld);assert(!a.collecting()); // a loading screen starts the warm-up again
        Arming s;s.endFrame(FK::WorldHash);for(int i=0;i<10;++i)s.endFrame(FK::WorldMissed);assert(!s.collecting()); // stock
        Arming m;unsigned armed=0;frames(m,FK::WorldMissed,Arming::kArmFrames,40,&armed);assert(m.armed()&&m.collecting());
    }
    std::puts("ui boundary ok");
    return 0;
}
