#include "celestial_halo_visibility.h"
#include "celestial_glow.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace NorthlightCelestialHalo;
static float update(float previous,float current,const float* c){
    if(current<=0)return 0;
    float result=c[0]>.5f?previous+(current-previous)*(current<previous?c[1]:c[2]):current;
    return current<=0&&result<.001f?0:result;
}
static float simulate(unsigned fps,float initial,float target){
    History h;float camera[3]={},c[4]={};h.advance(1000,0,1920,1080,camera,.5f,c);
    float value=initial;
    for(unsigned i=1;i<=fps;++i){h.advance(1000+unsigned(std::lround(i*1000./fps)),i,1920,1080,camera,.5f,c);value=update(value,target,c);}
    return value;
}
int main(){
    float sx=0,sy=0;std::vector<float> xs;
    for(unsigned i=0;i<Samples;++i){float x,y;offset(i,x,y);assert(x*x+y*y<1&&x*x+y*y>0);sx+=x;sy+=y;xs.push_back(x);}
    assert(std::fabs(sx/Samples)<.03&&std::fabs(sy/Samples)<.03);
    // Sweep a trunk edge across a large projected disc. Bilinear filtering
    // of binary sky masks bounds subpixel changes, unlike raw depth filtering.
    auto coverage=[&](float edge){float sum=0;for(float x:xs)sum+=std::clamp((x-edge)*80+.5f,0.f,1.f);return sum/Samples;};
    float last=coverage(-1.1f);assert(last==1);
    for(int i=1;i<=22000;++i){float now=coverage(-1.1f+i*.0001f);assert(now<=last+.000001f);assert(last-now<.001f);last=now;}
    assert(last==0);
    // Frame-rate independent convergence, including complete occlusion.
    for(unsigned fps:{30u,60u,144u,240u}){
        assert(simulate(fps,1,0)==0);
        assert(std::fabs(simulate(fps,0,1)-(1-std::exp(-1.f/.14f)))<.00001f);
    }
    History h;float c[4]={},pos[3]={};h.advance(100,0,1920,1080,pos,.5f,c);assert(c[0]==0);
    // Alternating twig masks should converge with small frame-to-frame jumps.
    float value=.5f;
    for(unsigned i=1;i<=120;++i){h.advance(100+i*8,i,1920,1080,pos,.5f,c);float next=update(value,i%2?.4f:.6f,c);assert(std::fabs(next-value)<.026f);value=next;}
    // A full blocker removes stale glare immediately, while partial masks smooth.
    value=1;
    assert(update(value,0,c)==0);
    for(unsigned i=121;i<=190;++i){h.advance(100+i*8,i,1920,1080,pos,.5f,c);float next=update(value,0,c);assert(next<=value);value=next;}
    assert(value==0);
    h.advance(1700,191,1920,1080,pos,.5f,c);assert(c[0]==1);
    h.advance(1710,193,1920,1080,pos,.5f,c);assert(c[0]==0); // skipped frame
    h.advance(1720,194,1280,720,pos,.5f,c);assert(c[0]==0); // resize
    pos[0]=200;h.advance(1730,195,1280,720,pos,.5f,c);assert(c[0]==0); // teleport
    h.advance(1740,196,1280,720,pos,.7f,c);assert(c[0]==0); // clock jump
    h.advance(2000,197,1280,720,pos,.7f,c);assert(c[0]==0); // stale
    History midnight;midnight.advance(100,0,100,100,pos,.999999f,c);midnight.advance(116,1,100,100,pos,.000001f,c);assert(c[0]==1);
    History wrap;wrap.advance(UINT32_MAX-7,0,100,100,pos,.5f,c);wrap.advance(8,1,100,100,pos,.5f,c);assert(c[0]==1&&c[1]>0&&c[1]<1);
    // wrap ring: 32 equal-area annulus taps at 2-4R, all inside the 20R
    // sun terrain mask (terrain-mask UV .4...6); the ring's own history resets
    // like the disc's. There is no moon ring (the moon has no veil), so the
    // 6R moon mask, which the halo PS reads without a range check, is never
    // sampled beyond its extent.
    {float rx=0,ry=0;for(unsigned i=0;i<Samples;++i){float x,y;NorthlightCelestialGlow::ringOffset(i,x,y);const float r2=x*x+y*y;
        assert(r2>NorthlightCelestialGlow::RingInner*NorthlightCelestialGlow::RingInner&&r2<NorthlightCelestialGlow::RingOuter*NorthlightCelestialGlow::RingOuter);
        const float u=.5f+.5f*x/NorthlightCelestialGlow::SunMaskRadius,v=.5f-.5f*y/NorthlightCelestialGlow::SunMaskRadius;assert(u>.29f&&u<.71f&&v>.29f&&v<.71f);rx+=x;ry+=y;}
     assert(std::fabs(rx/Samples)<.12f&&std::fabs(ry/Samples)<.12f&&NorthlightCelestialGlow::RingOuter<=NorthlightCelestialGlow::SunMaskRadius);
     History ring;ring.advance(100,0,1920,1080,pos,.5f,c);assert(c[0]==0);ring.advance(116,1,1920,1080,pos,.5f,c);assert(c[0]==1);ring.advance(140,3,1920,1080,pos,.5f,c);assert(c[0]==0);}
    std::puts("PASS halo: 32 fixed disc taps; 32 ring taps 2-4R inside the 20R sun mask; 22000 subpixel edges; FPS-independent smoothing; full occlusion; twig oscillation; first/skip/resize/teleport/clock/stale/wrap resets");
}
