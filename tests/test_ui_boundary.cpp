// 0.3.203 (task 17): native test of ui_boundary.h (fallback effect boundary learning). No D3D.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
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
    { // accepts: the stock UI state (z=1 zwrite=0, ortho rows) passes; Z writes, no constants or a perspective row do not
        assert(accepts(false,c));
        assert(!accepts(true,c));
        assert(accepts(false,c,true)&&!accepts(false,c,false)&&!accepts(true,c,true)); // ZENABLE on + ZFUNC not ALWAYS (a depth-tested draw) is no UI draw
        assert(accepts(false,c,false,false)&&!accepts(true,c,false,false)); // Disabled Z ignores a stale LESS function, but never permits Z writes.
        assert(!accepts(false,nullptr));
        ortho(p);p[15]=0;assert(!accepts(false,p));
    }
    { // Observed native Ctrl+R transform, distinct from the scalable UI VS.
        const float native[16]={2.f/3840,0,0,-.99974f,0,2.f/2160,0,-1.00046f,0,0,.0001f,.5f,0,0,0,1};
        assert(pixelSpaceRows(native,3840,2160,0,1));
        assert(!pixelSpaceRows(native,3840,2160,0,.94f));
        assert(!pixelSpaceRows(native,1920,1080,0,1));
        assert(!pixelSpaceRows(c,3840,2160,0,1));
        float invalid[16];for(unsigned i=0;i<16;++i)invalid[i]=native[i];invalid[14]=1;invalid[15]=0;
        assert(!pixelSpaceRows(invalid,3840,2160,0,1));
    }
    { // The background/portrait draw precedes Ctrl+R and uses logical UI units.
        const float scaled[16]={2.29469f,0,0,-1.00026f,0,4.07946f,0,-.999537f,0,0,-.001f,.5f,0,0,0,1};
        assert(!pixelSpaceRows(scaled,3840,2160,0,1)); // Previous gate misses this real UI draw.
        assert(fullWindowOrthoRows(scaled,3840,2160,0,1));
        assert(!fullWindowOrthoRows(scaled,3840,2160,0,.94f));
        assert(!fullWindowOrthoRows(scaled,2160,3840,0,1));
        for(float scale:{.5f,1.f,2.8125f,4405.8048f}){
            float rows[16];ortho(rows,3840/scale,2160/scale);
            assert(fullWindowOrthoRows(rows,3840,2160,0,1));
        }
        for(unsigned index:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}){
            float rows[16];for(unsigned i=0;i<16;++i)rows[i]=scaled[i];
            rows[index]+=index==0||index==5?1.f:.1f;
            assert(!fullWindowOrthoRows(rows,3840,2160,0,1));
        }
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
        assert(a.isBoundary(0xA,0x1)&&!a.isBoundary(0xB,0x9)&&a.isBoundary(0xA,0x9));
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
    { // Loading-screen stock UI must not poison learning, or disarm a learned in-world font shader.
        Arming a;unsigned armed=0;
        frames(a,FK::NoWorld,600,40,&armed);
        assert(!a.disarmed()&&!a.collecting()&&armed==0);
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            if(a.collecting())for(unsigned i=0;i<40;++i)a.noteCandidate(0xB,0x2);
            if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;
        }
        assert(armed==1&&a.isBoundary(0xB,0x2));
        frames(a,FK::NoWorld,600,40,&armed);
        assert(a.armed()&&!a.disarmed()&&a.isBoundary(0xB,0x2));
        frames(a,FK::WorldFallback,1);assert(a.armed());
        // A genuine stock boundary after world draws still takes precedence.
        frames(a,FK::WorldHash,1);assert(a.disarmed()&&!a.armed());
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
    { // (f) a destroyed VS loses the proof; replacing a PS keeps the verified UI VS.
        Arming a;unsigned armed=0;frames(a,FK::WorldMissed,Arming::kArmFrames,40,&armed);assert(a.armed());
        a.forget(0x77);assert(a.armed());a.forget(0xA);assert(a.learning());
        frames(a,FK::WorldMissed,Arming::kArmFrames,40,&armed);assert(a.armed()&&armed==2);
        a.forget(0x1);assert(a.armed()&&a.learnedPsCount()==0&&a.isBoundary(0xA,0x9));
        a.forget(0xA);assert(a.learning()&&!a.isBoundary(0xA,0x9));
        a.noteCandidate(0xC,0x3);a.noteCandidate(0xD,0x4);a.forget(0x3);
        Arming::Pair top[4];assert(a.topCandidates(top,4)==1&&top[0].vs==0xD);
    }
    { // (g) Neutral neither advances nor resets
        Arming a;unsigned armed=0;frames(a,FK::WorldMissed,60,40,&armed);frames(a,FK::Neutral,500,40,&armed);assert(armed==0&&a.candidateStreak()==60);
        frames(a,FK::WorldMissed,59,40,&armed);assert(armed==0);frames(a,FK::WorldMissed,1,40,&armed);assert(armed==1);
        assert(std::fabs(a.drawsPerFrame()-40.f)<.5f); // the 500 Neutral frames' draws are not in the density
    }
    { // (g2) Neutral frames (effects off) cannot raise the density: a rare pass (2 per counted frame) never arms, however long effects stay off
        Arming a;unsigned armed=0;
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            a.noteCandidate(0xA,0x1);a.noteCandidate(0xA,0x1);
            if(a.endFrame(FK::WorldMissed)==R::Armed)++armed;
            for(unsigned n=0;n<10;++n){for(int i=0;i<40;++i)a.noteCandidate(0xA,0x1);a.noteCandidate(0xE,0x5);if(a.endFrame(FK::Neutral)==R::Armed)++armed;}
        }
        assert(armed==0&&a.learning());
        Arming::Pair top[4];assert(a.topCandidates(top,4)==0); // after the failed arming; the Neutral-only pair never kept a slot
        Arming b;for(int i=0;i<40;++i)b.noteCandidate(0xE,0x5);b.endFrame(FK::Neutral);assert(b.topCandidates(top,4)==0);
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
    { // PS shares identify the UI VS; an infrequent FPS PS must trigger the same boundary.
        Arming a;
        for(unsigned f=0;f<Arming::kArmFrames;++f){
            for(int i=0;i<50;++i)a.noteCandidate(0xA,0x1);for(int i=0;i<20;++i)a.noteCandidate(0xA,0x2);a.noteCandidate(0xA,0x3);
            a.endFrame(FK::WorldMissed);
        }
        assert(a.armed()&&a.learnedPsCount()==2&&a.isBoundary(0xA,0x2)&&a.isBoundary(0xA,0x3));
        assert(a.isBoundary(0xA,0)&&!a.isBoundary(0xB,0x3));
    }
    { // Fixed-function draws have no VS identity to establish as screen-space UI.
        Arming a;for(unsigned f=0;f<Arming::kArmFrames;++f){for(unsigned n=0;n<40;++n)a.noteCandidate(0,0x1);a.endFrame(FK::WorldMissed);}
        assert(!a.armed()&&!a.isBoundary(0,0x1));
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
