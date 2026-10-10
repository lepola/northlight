#include "legacy_fog.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightLegacyFog;
static Constants known(float x,float y,float z,float r,float g,float b){Constants c;c.parameters[0]=x;c.parameters[1]=y;c.parameters[2]=z;c.parameters[3]=1;c.color[0]=r;c.color[1]=g;c.color[2]=b;return c;}
static bool identity(const Constants& c){Constants i;for(int k=0;k<4;++k)if(c.parameters[k]!=i.parameters[k]||c.color[k]!=i.color[k])return false;return true;}
static bool same(const Constants& a,const Constants& b){for(int k=0;k<4;++k)if(a.parameters[k]!=b.parameters[k]||a.color[k]!=b.color[k])return false;return true;}

int main(){
    const Constants A=known(-.002f,1,1,.5f,.6f,.7f),B=known(-.001f,.7f,1,.2f,.3f,.9f),U;
    {Hold h;assert(identity(h.update("m",U,false,100)));}                  // unknown with nothing held
    {Hold h;assert(same(h.update("m",A,true,1000),A));}                    // first readable value as is
    {Hold h;h.update("m",A,true,1000);assert(same(h.update("m",B,true,1016),B));  // readable values pass through, no easing
     assert(same(h.update("m",A,true,1032),A));assert(same(h.update("m",B,true,1032+250),B));}
    {Hold h;h.update("m",A,true,1000);                                     // hold through unreadable frames within HoldMs
     for(std::uint32_t t=1016;t<=1000+HoldMs;t+=16)assert(same(h.update("m",U,false,t),A));}
    {Hold h;h.update("m",A,true,1000);Constants prev=A;std::uint32_t t=1000+HoldMs;   // post-hold fade of w to exact identity
     bool sawFade=false;int steps=0;
     for(;;){t+=16;Constants r=h.update("m",U,false,t);if(identity(r))break;assert(++steps<2000);
         assert(r.parameters[3]<=prev.parameters[3]&&r.parameters[3]>0);assert(r.parameters[0]==A.parameters[0]&&r.color[2]==A.color[2]);
         sawFade|=r.parameters[3]<1;prev=r;}
     assert(sawFade);assert(prev.parameters[3]>=1.f/256);
     assert(identity(h.update("m",U,false,t+16)));                         // held value dropped
     assert(same(h.update("m",B,true,t+32),B));}                           // readable again: as is
    {Hold h;h.update("m",A,true,1000);std::uint32_t t=1000+HoldMs;Constants r;  // readable after expiry, mid-fade: snaps, w not faded back up
     for(int i=0;i<10;++i)r=h.update("m",U,false,t+=16);assert(r.parameters[3]<1&&r.parameters[3]>0);
     assert(same(h.update("m",B,true,t+16),B));}
    {Hold h;h.update("m",A,true,1000);assert(same(h.update("m",B,true,1000+HoldMs+1),B));}
    {Hold h;const Constants off=U;                                         // known fog-off passes through, then is held as fog-off
     h.update("m",A,true,1000);assert(identity(h.update("m",off,true,1016)));
     assert(identity(h.update("m",U,false,1032)));assert(identity(h.update("m",U,false,1016+HoldMs)));
     assert(identity(h.update("m",U,false,1016+HoldMs+16)));
     assert(same(h.update("m",A,true,1016+HoldMs+32),A));}
    {Hold h;h.update("m",A,true,1000);assert(same(h.update("other",B,true,1016),B));  // map change: readable as is
     assert(identity(h.update("third",U,false,1032)));}                    // unreadable: identity at once
    {Hold h;h.update("m",A,true,1000);assert(identity(h.update("other",U,false,1016)));assert(identity(h.update("m",U,false,1032)));}
    {Hold h;h.update("m",A,true,1000);Constants r=h.update("m",U,false,1000+HoldMs+250); // fade step is bounded by the dt clamp
     const float f=1-std::exp(-250.f*.001f/FadeSeconds);assert(std::fabs(r.parameters[3]-(1-f))<1e-6f);
     Hold g;g.update("m",A,true,1000);g.update("m",U,false,1000+HoldMs);r=g.update("m",U,false,1000+HoldMs+100000);  // long gap: still one clamped step
     assert(std::fabs(r.parameters[3]-(1-f))<1e-6f);}
    {Hold h;const std::uint32_t t0=0xfffffff0u;h.update("m",A,true,t0);     // uint32 wrap-around
     assert(same(h.update("m",U,false,t0+40),A));assert(same(h.update("m",U,false,t0+HoldMs),A));
     assert(!same(h.update("m",U,false,t0+HoldMs+300),A));
     Hold g;g.update("m",A,true,t0);assert(same(g.update("m",B,true,t0+16),B));}
    {Hold h;h.update("m",A,true,1000);const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
     Constants bad=B;bad.parameters[1]=nan;assert(same(h.update("m",bad,true,1016),A));  // non-finite = unknown, held value stays
     bad=B;bad.color[2]=inf;assert(same(h.update("m",bad,true,1032),A));
     bad=B;bad.parameters[3]=nan;assert(same(h.update("m",bad,true,1048),A));
     assert(finite(h.update("m",B,true,1064)));
     Hold g;bad=B;bad.parameters[0]=nan;assert(identity(g.update("m",bad,true,10)));}
    {Hold h;h.update("m",A,true,5);h.reset();assert(identity(h.update("m",U,false,6)));}
    std::puts("legacy fog hold ok");return 0;
}
